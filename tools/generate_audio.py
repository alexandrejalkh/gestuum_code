"""
⚠️ DEPRECATED — Use generate_google_tts.py instead.

This script uses edge-tts (Microsoft) which produces lower quality child voices.
The project migrated to Google Cloud TTS on 16/Mar/2026.

To generate audio: python generate_google_tts.py --key YOUR_API_KEY

---
GESTUUM — Audio Generator (LEGACY - edge-tts)
Generates WAV files (11kHz, 16-bit, mono) using Microsoft Edge TTS.

Voices:
    homem  — pt-BR-AntonioNeural (natural)
    mulher — pt-BR-FranciscaNeural (natural)
    menino — pt-BR-AntonioNeural + pitch shift +8 semitones
    menina — pt-BR-FranciscaNeural + pitch shift +10 semitones

Requirements:
    pip install edge-tts
    ffmpeg must be installed and in PATH

Usage:
    python generate_audio.py                          # all 4 voices x 90 audio gestures
    python generate_audio.py --voice homem             # only 1 voice
    python generate_audio.py --voice menino --profile base  # only base profiles for menino

Output:
    gestuum/sensor_a/data/audio/homem/*.wav
    gestuum/sensor_a/data/audio/mulher/*.wav
    gestuum/sensor_a/data/audio/menino/*.wav
    gestuum/sensor_a/data/audio/menina/*.wav

    Only 1 voice folder is uploaded to SPIFFS at a time (app switches).

Notes:
    92 total gestures, but 2 are automation-only (led_ligar, led_desligar)
    with no spoken text, so 90 audio files are generated per voice.
    Total: 90 x 4 voices = 360 audio files.
"""

import argparse
import asyncio
import math
import os
import subprocess
import sys

# ---------------------------------------------------------------------------
# Voice definitions
# ---------------------------------------------------------------------------
VOICES = {
    "homem": {
        "tts_voice": "pt-BR-AntonioNeural",
        "pitch_shift": 0,
        "gender": "male",
    },
    "mulher": {
        "tts_voice": "pt-BR-FranciscaNeural",
        "pitch_shift": 0,
        "gender": "female",
    },
    "menino": {
        "tts_voice": "pt-BR-AntonioNeural",
        "pitch_shift": 8,
        "gender": "male",
    },
    "menina": {
        "tts_voice": "pt-BR-FranciscaNeural",
        "pitch_shift": 10,
        "gender": "female",
    },
}

# ---------------------------------------------------------------------------
# Gender-specific spoken text overrides (for female voices: mulher, menina)
# ---------------------------------------------------------------------------
FEMALE_OVERRIDES = {
    "obrigado": "Obrigada",
    "tonto": "Tonta",
}

# ---------------------------------------------------------------------------
# All 92 gestures organized by profile
# Each entry: (filename, spoken_text)
# spoken_text=None means automation-only gesture (no audio generated)
# ---------------------------------------------------------------------------
PROFILES = {
    "base_geral": [
        ("agua", "Água"),
        ("comida", "Comida"),
        ("banheiro", "Banheiro"),
        ("remedio", "Remédio"),
        ("descansar", "Descansar"),
        ("dormir", "Dormir"),
        ("bem", "Bem"),
        ("mal", "Mal"),
        ("fome", "Fome"),
        ("sede", "Sede"),
    ],
    "base_emergencia": [
        ("ajuda", "Ajuda"),
        ("socorro", "Socorro!"),
        ("medico", "Médico"),
        ("dor", "Dor"),
        ("muita_dor", "Muita dor!"),
        ("tonto", "Tonto"),
        ("cabeca", "Cabeça"),
        ("peito", "Peito"),
        ("nao_respiro", "Não consigo respirar!"),
        ("ambulancia", "Ambulância!"),
    ],
    "base_casa": [
        ("quarto", "Quarto"),
        ("sala", "Sala"),
        ("cozinha", "Cozinha"),
        ("luz", "Luz"),
        ("tv", "Televisão"),
        ("ar", "Ar condicionado"),
        ("ligar", "Ligar"),
        ("desligar", "Desligar"),
        ("porta", "Porta"),
        ("janela", "Janela"),
    ],
    "base_trabalho": [
        ("reuniao", "Reunião"),
        ("escritorio", "Escritório"),
        ("computador", "Computador"),
        ("documento", "Documento"),
        ("email", "E-mail"),
        ("cafe", "Café"),
        ("pausa", "Pausa"),
        ("comecar", "Começar"),
        ("terminar", "Terminar"),
        ("ajuda_trab", "Preciso de ajuda"),
    ],
    "base_social": [
        ("oi", "Oi!"),
        ("tchau", "Tchau!"),
        ("bom_dia", "Bom dia!"),
        ("obrigado", "Obrigado"),
        ("por_favor", "Por favor"),
        ("sim", "Sim"),
        ("nao", "Não"),
        ("ok", "Ok"),
        ("feliz", "Feliz"),
        ("triste", "Triste"),
    ],
    "hospital": [
        ("enfermeira", "Enfermeira"),
        ("medico_plantao", "Médico de plantão"),
        ("soro", "Soro"),
        ("exame", "Exame"),
        ("visita", "Visita"),
        ("alta", "Alta"),
        ("posicao_cama", "Mudar posição da cama"),
        ("cobertor", "Cobertor"),
        ("leito", "Leito"),
        ("curativo", "Curativo"),
        ("alergia_hosp", "Tenho alergia"),
        ("nausea", "Estou com náusea"),
        ("dieta", "Dieta"),
    ],
    "escola": [
        ("professor", "Professor"),
        ("aula", "Aula"),
        ("intervalo", "Intervalo"),
        ("licao", "Lição"),
        ("brincar", "Brincar"),
        ("colega", "Colega"),
        ("mochila", "Mochila"),
        ("quadra", "Quadra"),
        ("cantina", "Cantina"),
        ("diretor", "Diretor"),
        ("alergia_escola", "Tenho alergia alimentar"),
        ("lanche", "Lanche"),
        ("prova", "Prova"),
    ],
    "transporte": [
        ("motorista", "Motorista"),
        ("parada", "Parada"),
        ("destino", "Destino"),
        ("endereco", "Endereço"),
        ("mapa", "Mapa"),
        ("chegou", "Chegou"),
        ("esperar", "Esperar"),
        ("bilhete", "Bilhete"),
        ("aeroporto", "Aeroporto"),
        ("hotel", "Hotel"),
        ("taxi", "Táxi"),
        ("onibus", "Ônibus"),
    ],
    "automacao": [
        ("led_ligar", None),
        ("led_desligar", None),
    ],
}

# Group names that belong to "base" meta-profile
BASE_PROFILES = [
    "base_geral",
    "base_emergencia",
    "base_casa",
    "base_trabalho",
    "base_social",
]

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
AUDIO_BASE_DIR = os.path.join(PROJECT_ROOT, "sensor_a", "data", "audio")
TEMP_DIR = os.path.join(SCRIPT_DIR, "_temp_tts")

# SPIFFS size limit per voice folder (4 MB default)
SPIFFS_LIMIT_BYTES = 4_000_000


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


def get_gestures_for_profile(profile_filter: str | None) -> list[tuple[str, str | None]]:
    """Return the list of (filename, spoken_text) tuples for the given profile filter.

    If profile_filter is None, return all 92 gestures.
    If profile_filter is 'base', return all 5 base profiles (50 gestures).
    Otherwise match the exact profile name.

    Entries with spoken_text=None are automation-only (no audio generated).
    """
    if profile_filter is None:
        gestures = []
        for profile_name in PROFILES:
            gestures.extend(PROFILES[profile_name])
        return gestures

    profile_filter = profile_filter.lower().strip()

    if profile_filter == "base":
        gestures = []
        for name in BASE_PROFILES:
            gestures.extend(PROFILES[name])
        return gestures

    if profile_filter in PROFILES:
        return list(PROFILES[profile_filter])

    # Try matching with base_ prefix
    prefixed = f"base_{profile_filter}"
    if prefixed in PROFILES:
        return list(PROFILES[prefixed])

    available = list(PROFILES.keys()) + ["base"]
    print(f"ERROR: Unknown profile '{profile_filter}'.")
    print(f"Available profiles: {', '.join(available)}")
    sys.exit(1)


def apply_female_overrides(
    gestures: list[tuple[str, str | None]], voice_name: str
) -> list[tuple[str, str | None]]:
    """Apply gender-specific text overrides for female voices."""
    if VOICES[voice_name]["gender"] != "female":
        return gestures

    result = []
    for filename, text in gestures:
        if text is not None and filename in FEMALE_OVERRIDES:
            result.append((filename, FEMALE_OVERRIDES[filename]))
        else:
            result.append((filename, text))
    return result


# ---------------------------------------------------------------------------
# TTS generation
# ---------------------------------------------------------------------------
async def generate_tts(filename: str, text: str, tts_voice: str) -> str:
    """Generate TTS audio using edge-tts and return the temp MP3 path."""
    import edge_tts

    mp3_path = os.path.join(TEMP_DIR, f"{filename}.mp3")
    # Rate -25% makes speech slower and clearer
    communicate = edge_tts.Communicate(text, tts_voice, rate="-25%")
    await communicate.save(mp3_path)
    return mp3_path


# ---------------------------------------------------------------------------
# Audio conversion
# ---------------------------------------------------------------------------
def convert_to_wav(mp3_path: str, wav_path: str, pitch_shift: int) -> bool:
    """Convert MP3 to 11kHz 16-bit mono WAV.

    - 200ms silence before speech (adelay=200|200)
    - 150ms silence after speech (apad=pad_dur=0.15)
    - For pitch-shifted voices (menino/menina), use asetrate to shift pitch
      then aresample back to 11025 Hz.

    Args:
        mp3_path: Path to input MP3 file.
        wav_path: Path to output WAV file.
        pitch_shift: Pitch shift in semitones (0 = no shift).
    """
    if pitch_shift > 0:
        # Pitch shift using asetrate:
        # Multiply sample rate by 2^(semitones/12) to shift pitch up,
        # then resample back to 11025 Hz.
        factor = math.pow(2, pitch_shift / 12.0)
        # We need to know the original sample rate. Edge TTS outputs 24kHz MP3.
        # asetrate changes the declared rate without resampling, which speeds up
        # and raises pitch. Then aresample brings it back to 11025 Hz.
        new_rate = int(24000 * factor)
        af_chain = (
            f"asetrate={new_rate},"
            f"aresample=11025,"
            f"adelay=200|200,"
            f"apad=pad_dur=0.15"
        )
    else:
        af_chain = "adelay=200|200,apad=pad_dur=0.15"

    cmd = [
        FFMPEG_PATH,
        "-y",
        "-i",
        mp3_path,
        "-af",
        af_chain,
        "-ar",
        "11025",
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
async def generate_voice(
    voice_name: str, gestures: list[tuple[str, str | None]]
) -> tuple[int, int, int, int]:
    """Generate all audio files for a single voice.

    Skips automation-only gestures (spoken_text=None).

    Returns:
        Tuple of (success_count, failed_count, skipped_count, total_bytes).
    """
    voice_cfg = VOICES[voice_name]
    tts_voice = voice_cfg["tts_voice"]
    pitch_shift = voice_cfg["pitch_shift"]

    # Apply gender-specific overrides
    gestures = apply_female_overrides(gestures, voice_name)

    voice_dir = os.path.join(AUDIO_BASE_DIR, voice_name)
    os.makedirs(voice_dir, exist_ok=True)

    # Separate audio gestures from automation-only gestures
    audio_gestures = [(f, t) for f, t in gestures if t is not None]
    skipped_gestures = [(f, t) for f, t in gestures if t is None]

    total = len(audio_gestures)
    skipped = len(skipped_gestures)
    success = 0
    failed = 0
    total_size = 0

    print(f"\n{'=' * 60}")
    print(f"  Voice: {voice_name} ({tts_voice})")
    if pitch_shift > 0:
        print(f"  Pitch shift: +{pitch_shift} semitones")
    print(f"  Audio gestures: {total}")
    if skipped > 0:
        print(f"  Skipped (automation-only): {skipped}")
    print(f"  Output: {voice_dir}")
    print(f"{'=' * 60}\n")

    if skipped > 0:
        for filename, _ in skipped_gestures:
            print(f"  [SKIP] {filename} (automation-only, no audio)")
        print()

    for i, (filename, text) in enumerate(audio_gestures, 1):
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

    return success, failed, skipped, total_size


# ---------------------------------------------------------------------------
# Summary printer
# ---------------------------------------------------------------------------
def print_summary(results: dict[str, tuple[int, int, int, int]]) -> None:
    """Print generation summary with SPIFFS fit check.

    Args:
        results: Dict mapping voice_name -> (success, failed, skipped, total_bytes).
    """
    grand_success = 0
    grand_failed = 0
    grand_skipped = 0
    grand_size = 0

    print(f"\n{'=' * 60}")
    print("  GENERATION SUMMARY")
    print(f"{'=' * 60}\n")

    for voice_name, (success, failed, skipped, total_bytes) in results.items():
        grand_success += success
        grand_failed += failed
        grand_skipped += skipped
        grand_size += total_bytes

        size_kb = total_bytes // 1024
        size_mb = total_bytes / 1024 / 1024
        fits = total_bytes <= SPIFFS_LIMIT_BYTES
        status = "OK" if fits else "EXCEEDS LIMIT"

        print(f"  {voice_name:8s}: {success:3d} OK, {failed:3d} failed, "
              f"{skipped:3d} skipped | "
              f"{size_kb:5d} KB ({size_mb:.1f} MB) | "
              f"SPIFFS: {status}")

    print()
    print(f"  {'TOTAL':8s}: {grand_success:3d} OK, {grand_failed:3d} failed, "
          f"{grand_skipped:3d} skipped | "
          f"{grand_size // 1024:5d} KB ({grand_size / 1024 / 1024:.1f} MB)")
    print()

    # SPIFFS fit check per voice
    any_over = False
    for voice_name, (_, _, _, total_bytes) in results.items():
        if total_bytes > SPIFFS_LIMIT_BYTES:
            any_over = True
            excess = total_bytes - SPIFFS_LIMIT_BYTES
            print(f"  WARNING: '{voice_name}' exceeds {SPIFFS_LIMIT_BYTES // 1024 // 1024} MB "
                  f"SPIFFS limit by {excess // 1024} KB!")

    if any_over:
        print()
        print("  Consider:")
        print("    - Using a larger SPIFFS partition in partitions.csv")
        print("    - Shortening phrases")
        print("    - Reducing sample rate further (change -ar 11025 to -ar 8000)")
    else:
        print("  All voice folders fit within the "
              f"{SPIFFS_LIMIT_BYTES // 1024 // 1024} MB SPIFFS limit.")

    print()
    print("  NOTE: Only 1 voice folder is uploaded to SPIFFS at a time.")
    print("        The app switches between voices at runtime.")
    print()
    print("  Next steps:")
    print("    1. Choose a voice folder to upload (e.g., homem/)")
    print("    2. Copy chosen folder contents to sensor_a/data/audio/")
    print("    3. Upload to ESP32: pio run -t uploadfs")
    print(f"{'=' * 60}")


# ---------------------------------------------------------------------------
# CLI and main
# ---------------------------------------------------------------------------
def parse_args() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser = argparse.ArgumentParser(
        description="Generate GESTUUM audio files for 92 gestures x 4 voices "
                    "(90 audio + 2 automation-only)."
    )
    parser.add_argument(
        "--voice",
        choices=list(VOICES.keys()),
        default=None,
        help="Generate only for a specific voice. Default: all 4 voices.",
    )
    parser.add_argument(
        "--profile",
        default=None,
        help=(
            "Generate only for a specific profile. "
            "Options: base (all 5 base profiles), base_geral, base_emergencia, "
            "base_casa, base_trabalho, base_social, hospital, escola, "
            "transporte, automacao. Default: all profiles."
        ),
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

    # Determine which gestures to generate
    gestures = get_gestures_for_profile(args.profile)

    # Count audio vs automation-only
    audio_count = sum(1 for _, t in gestures if t is not None)
    auto_count = sum(1 for _, t in gestures if t is None)

    # Determine which voices to generate
    if args.voice:
        voice_names = [args.voice]
    else:
        voice_names = list(VOICES.keys())

    print(f"\nGESTUUM Audio Generator")
    print(f"  Voices:   {', '.join(voice_names)}")
    print(f"  Gestures: {len(gestures)} total ({audio_count} audio, {auto_count} automation-only)")
    print(f"  Total:    {audio_count * len(voice_names)} audio files")
    print(f"  Format:   11kHz, 16-bit, mono WAV")
    print(f"  Pre-silence: 200ms | Post-silence: 150ms")
    print(f"  Speech rate: -25%")

    # Generate for each voice
    results: dict[str, tuple[int, int, int, int]] = {}
    for voice_name in voice_names:
        success, failed, skipped, total_bytes = await generate_voice(voice_name, gestures)
        results[voice_name] = (success, failed, skipped, total_bytes)

    # Cleanup temp directory
    if os.path.isdir(TEMP_DIR):
        for f in os.listdir(TEMP_DIR):
            filepath = os.path.join(TEMP_DIR, f)
            if os.path.isfile(filepath):
                os.remove(filepath)
        os.rmdir(TEMP_DIR)

    # Print summary
    print_summary(results)


if __name__ == "__main__":
    asyncio.run(main())
