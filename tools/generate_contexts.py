"""
GESTUUM — Context Audio Generator for 20 Left-Hand Contexts x 4 Voices
Generates WAV files (8kHz, 16-bit, mono) using Microsoft Edge TTS.

These are the LEFT HAND context phrases that precede the RIGHT HAND object
gestures. Example: "Preciso de" + "Agua" = "Preciso de agua".

Voices:
    homem  — pt-BR-AntonioNeural (natural)
    mulher — pt-BR-FranciscaNeural (natural)
    menino — pt-BR-AntonioNeural + pitch shift +8 semitones
    menina — pt-BR-FranciscaNeural + pitch shift +10 semitones

Requirements:
    pip install edge-tts
    ffmpeg must be installed and in PATH

Usage:
    python generate_contexts.py                # all 4 voices x 20 contexts
    python generate_contexts.py --voice homem  # only 1 voice

Output:
    gestuum/sensor_a/data/audio/homem/ctx_e01_preciso_de.wav
    gestuum/sensor_a/data/audio/mulher/ctx_e01_preciso_de.wav
    gestuum/sensor_a/data/audio/menino/ctx_e01_preciso_de.wav
    gestuum/sensor_a/data/audio/menina/ctx_e01_preciso_de.wav
    ...

    Context files live alongside object files in the same voice folder,
    distinguished by the ctx_ prefix. The firmware plays them by filename.
"""

import argparse
import asyncio
import math
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# Voice definitions (same as generate_audio.py)
# ---------------------------------------------------------------------------
VOICES = {
    "homem": {
        "tts_voice": "pt-BR-AntonioNeural",
        "pitch_shift": 0,
    },
    "mulher": {
        "tts_voice": "pt-BR-FranciscaNeural",
        "pitch_shift": 0,
    },
    "menino": {
        "tts_voice": "pt-BR-AntonioNeural",
        "pitch_shift": 8,
    },
    "menina": {
        "tts_voice": "pt-BR-FranciscaNeural",
        "pitch_shift": 10,
    },
}

# ---------------------------------------------------------------------------
# 20 Left-Hand Contexts
# Each entry: (filename_suffix, spoken_text)
# Output filename will be ctx_{filename_suffix}.wav
# ---------------------------------------------------------------------------
CONTEXTS = [
    ("e01_preciso_de", "Preciso de"),
    ("e02_quero", "Quero"),
    ("e03_posso", "Posso"),
    ("e04_vou", "Vou"),
    ("e05_estou", "Estou"),
    ("e06_estou_com", "Estou com"),
    ("e07_nao_preciso_de", "Não preciso de"),
    ("e08_nao_quero", "Não quero"),
    ("e09_nao_posso", "Não posso"),
    ("e10_nao_vou", "Não vou"),
    ("e11_nao_estou", "Não estou"),
    ("e12_nao_estou_com", "Não estou com"),
    ("e13_onde_esta", "Onde está"),
    ("e14_quando", "Quando"),
    ("e15_quem", "Quem"),
    ("e16_como", "Como"),
    ("e17_o_que_e", "O que é"),
    ("e18_por_que", "Por que"),
    ("e19_chame", "Chame"),
    ("e20_avise", "Avise"),
]

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
# FIX: Paths encurtados para SPIFFS (max 31 chars)
# Mapeamento: homem→h, mulher→m, menino→n, menina→i
AUDIO_BASE_DIR = os.path.join(PROJECT_ROOT, "sensor_a", "data", "a")
VOICE_DIR_MAP = {"homem": "h", "mulher": "m", "menino": "n", "menina": "i"}
TEMP_DIR = os.path.join(SCRIPT_DIR, "_temp_tts_ctx")


# ---------------------------------------------------------------------------
# Utilities
# ---------------------------------------------------------------------------
def find_ffmpeg() -> str:
    """Find ffmpeg executable, checking PATH and common install locations."""
    import shutil

    path = shutil.which("ffmpeg")
    if path:
        return path
    # WinGet install location
    winget_path = os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages")
    if os.path.isdir(winget_path):
        for root, dirs, files in os.walk(winget_path):
            if "ffmpeg.exe" in files:
                return os.path.join(root, "ffmpeg.exe")
    return "ffmpeg"


FFMPEG_PATH = find_ffmpeg()


# ---------------------------------------------------------------------------
# TTS generation
# ---------------------------------------------------------------------------
async def generate_tts(filename: str, text: str, tts_voice: str) -> str:
    """Generate TTS audio using edge-tts and return the temp MP3 path."""
    import edge_tts

    mp3_path = os.path.join(TEMP_DIR, f"{filename}.mp3")
    # Rate -20% (slightly faster than objects which use -25%)
    communicate = edge_tts.Communicate(text, tts_voice, rate="-20%")
    await communicate.save(mp3_path)
    return mp3_path


# ---------------------------------------------------------------------------
# Audio conversion
# ---------------------------------------------------------------------------
def convert_to_wav(mp3_path: str, wav_path: str, pitch_shift: int) -> bool:
    """Convert MP3 to 8kHz 16-bit mono WAV.

    - 200ms silence before speech (adelay=200|200)
    - 100ms silence after speech (apad=pad_dur=0.1)
    - For pitch-shifted voices (menino/menina), use asetrate to shift pitch
      then aresample back to 8000 Hz.

    Args:
        mp3_path: Path to input MP3 file.
        wav_path: Path to output WAV file.
        pitch_shift: Pitch shift in semitones (0 = no shift).
    """
    # FIX H15: Padronizado para 11025Hz em TODAS as vozes.
    # Antes: adultos geravam a 8kHz, criancas a 11025Hz.
    # O firmware espera 11025Hz (AUDIO_SAMPLE_RATE em constants.h).
    # Audio a 8kHz tocava 38% mais rapido e agudo no dispositivo.
    out_rate = 11025
    if pitch_shift > 0:
        factor = math.pow(2, pitch_shift / 12.0)
        new_rate = int(24000 * factor)
        af_chain = (
            f"asetrate={new_rate},"
            f"aresample={out_rate},"
            f"adelay=200|200,"
            f"apad=pad_dur=0.1"
        )
    else:
        af_chain = f"aresample={out_rate},adelay=200|200,apad=pad_dur=0.1"

    cmd = [
        FFMPEG_PATH,
        "-y",
        "-i",
        mp3_path,
        "-af",
        af_chain,
        "-ar",
        str(out_rate),
        "-ac",
        "1",
        "-sample_fmt",
        "s16",
        "-f",
        "wav",
        wav_path,
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    return result.returncode == 0


# ---------------------------------------------------------------------------
# Main generation loop for a single voice
# ---------------------------------------------------------------------------
async def generate_voice(voice_name: str) -> tuple[int, int, int]:
    """Generate all 20 context audio files for a single voice.

    Returns:
        Tuple of (success_count, failed_count, total_bytes).
    """
    voice_cfg = VOICES[voice_name]
    tts_voice = voice_cfg["tts_voice"]
    pitch_shift = voice_cfg["pitch_shift"]

    # Usa codigo curto da pasta para SPIFFS
    voice_code = VOICE_DIR_MAP.get(voice_name, voice_name)
    voice_dir = os.path.join(AUDIO_BASE_DIR, voice_code)
    os.makedirs(voice_dir, exist_ok=True)

    total = len(CONTEXTS)
    success = 0
    failed = 0
    total_size = 0

    print(f"\n{'=' * 60}")
    print(f"  Voice: {voice_name} ({tts_voice})")
    if pitch_shift > 0:
        print(f"  Pitch shift: +{pitch_shift} semitones")
    print(f"  Contexts: {total}")
    print(f"  Output: {voice_dir}")
    print(f"{'=' * 60}\n")

    for i, (suffix, text) in enumerate(CONTEXTS, 1):
        filename = f"ctx_{suffix}"
        wav_path = os.path.join(voice_dir, f"{filename}.wav")
        print(f"  [{i:3d}/{total}] {filename}.wav <- \"{text}\"", end=" ... ")

        try:
            # Generate TTS
            mp3_path = await generate_tts(filename, text, tts_voice)

            # Convert to WAV (with optional pitch shift)
            if convert_to_wav(mp3_path, wav_path, pitch_shift):
                file_size = os.path.getsize(wav_path)
                total_size += file_size
                print(f"OK ({file_size // 1024} KB)")
                success += 1
            else:
                print("FAILED (ffmpeg conversion)")
                failed += 1

        except Exception as e:
            print(f"FAILED ({e})")
            failed += 1

    return success, failed, total_size


# ---------------------------------------------------------------------------
# Summary printer
# ---------------------------------------------------------------------------
def print_summary(results: dict[str, tuple[int, int, int]]) -> None:
    """Print generation summary.

    Args:
        results: Dict mapping voice_name -> (success, failed, total_bytes).
    """
    grand_success = 0
    grand_failed = 0
    grand_size = 0

    print(f"\n{'=' * 60}")
    print("  CONTEXT GENERATION SUMMARY")
    print(f"{'=' * 60}\n")

    for voice_name, (success, failed, total_bytes) in results.items():
        grand_success += success
        grand_failed += failed
        grand_size += total_bytes

        size_kb = total_bytes // 1024
        print(f"  {voice_name:8s}: {success:3d} OK, {failed:3d} failed | "
              f"{size_kb:5d} KB")

    print()
    print(f"  {'TOTAL':8s}: {grand_success:3d} OK, {grand_failed:3d} failed | "
          f"{grand_size // 1024:5d} KB ({grand_size / 1024 / 1024:.1f} MB)")
    print()
    print("  Context files (ctx_*.wav) live alongside object files in the")
    print("  same voice folder. The firmware plays them by filename.")
    print(f"{'=' * 60}")


# ---------------------------------------------------------------------------
# CLI and main
# ---------------------------------------------------------------------------
def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Generate GESTUUM context audio files for 20 contexts x 4 voices."
    )
    parser.add_argument(
        "--voice",
        choices=list(VOICES.keys()),
        default=None,
        help="Generate only for a specific voice. Default: all 4 voices.",
    )
    return parser.parse_args()


async def main() -> None:
    args = parse_args()

    # Check dependencies
    try:
        import edge_tts  # noqa: F401
    except ImportError:
        print("ERROR: edge-tts not installed.")
        print("Run: pip install edge-tts")
        sys.exit(1)

    try:
        subprocess.run([FFMPEG_PATH, "-version"], capture_output=True, check=True)
        print(f"ffmpeg found: {FFMPEG_PATH}")
    except (FileNotFoundError, subprocess.CalledProcessError):
        print("ERROR: ffmpeg not found.")
        print("Install ffmpeg: https://ffmpeg.org/download.html")
        sys.exit(1)

    # Create temp directory
    os.makedirs(TEMP_DIR, exist_ok=True)

    # Determine which voices to generate
    if args.voice:
        voice_names = [args.voice]
    else:
        voice_names = list(VOICES.keys())

    print(f"\nGESTUUM Context Audio Generator")
    print(f"  Voices:     {', '.join(voice_names)}")
    print(f"  Contexts:   {len(CONTEXTS)}")
    print(f"  Total:      {len(CONTEXTS) * len(voice_names)} audio files")
    print(f"  Format:     8kHz, 16-bit, mono WAV")
    print(f"  Pre-silence:  200ms | Post-silence: 100ms")
    print(f"  Speech rate:  -20%")

    # Generate for each voice
    results: dict[str, tuple[int, int, int]] = {}
    for voice_name in voice_names:
        success, failed, total_bytes = await generate_voice(voice_name)
        results[voice_name] = (success, failed, total_bytes)

    # Cleanup temp directory
    if os.path.isdir(TEMP_DIR):
        import shutil
        try:
            shutil.rmtree(TEMP_DIR)
        except OSError:
            print(f"  NOTE: Could not remove temp dir {TEMP_DIR} (non-critical).")

    # Print summary
    print_summary(results)


if __name__ == "__main__":
    asyncio.run(main())
