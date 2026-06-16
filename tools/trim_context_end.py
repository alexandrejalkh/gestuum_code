"""
GESTUUM — Remove 200ms from END of context WAVs (left hand)
Trims the trailing silence to reduce gap between context and object audio.
Only affects ctx_* files.

Usage:
    python trim_context_end.py                  # all 4 voices
    python trim_context_end.py --voice homem    # single voice
"""

import os
import subprocess
import shutil
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
AUDIO_BASE = os.path.join(os.path.dirname(SCRIPT_DIR), "sensor_a", "data", "audio")

VOICES = ["homem", "mulher", "menino", "menina"]

FFMPEG = shutil.which("ffmpeg")
if not FFMPEG:
    winget_path = os.path.expandvars(r"%LOCALAPPDATA%\Microsoft\WinGet\Packages")
    if os.path.isdir(winget_path):
        for root, dirs, files in os.walk(winget_path):
            if "ffmpeg.exe" in files:
                FFMPEG = os.path.join(root, "ffmpeg.exe")
                break
if not FFMPEG:
    FFMPEG = "ffmpeg"


def get_duration(wav_path):
    """Get WAV duration in seconds by reading WAV header for sample rate."""
    try:
        import struct
        with open(wav_path, 'rb') as f:
            f.read(24)  # Skip to byte 24
            sample_rate = struct.unpack('<I', f.read(4))[0]  # Read sample rate
        file_size = os.path.getsize(wav_path)
        data_size = file_size - 44  # WAV header = 44 bytes
        if data_size <= 0 or sample_rate == 0:
            return None
        bytes_per_sec = sample_rate * 1 * 2  # mono, 16-bit
        return data_size / bytes_per_sec
    except Exception:
        return None


def trim_end(wav_path, trim_ms=200):
    """Remove last trim_ms milliseconds from a WAV file."""
    duration = get_duration(wav_path)
    if duration is None:
        return False

    new_duration = duration - (trim_ms / 1000.0)
    if new_duration <= 0.1:
        return False

    directory = os.path.dirname(wav_path)
    basename = os.path.basename(wav_path)
    tmp_path = os.path.join(directory, "_tmp_" + basename)

    # Detect sample rate from WAV header
    import struct
    try:
        with open(wav_path, 'rb') as f:
            f.read(24)
            sample_rate = struct.unpack('<I', f.read(4))[0]
    except Exception:
        sample_rate = 8000

    cmd = [
        FFMPEG, "-y",
        "-i", wav_path,
        "-t", str(new_duration),
        "-c:a", "pcm_s16le",
        "-ar", str(sample_rate),
        "-ac", "1",
        tmp_path
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)

    if result.returncode == 0 and os.path.exists(tmp_path):
        os.replace(tmp_path, wav_path)
        return True
    else:
        if os.path.exists(tmp_path):
            os.remove(tmp_path)
        return False


def process_voice(voice, trim_ms=200):
    voice_dir = os.path.join(AUDIO_BASE, voice)
    if not os.path.isdir(voice_dir):
        print(f"  {voice}: pasta nao encontrada")
        return 0, 0

    files = sorted([f for f in os.listdir(voice_dir)
                    if f.endswith(".wav") and f.startswith("ctx_")])

    success = 0
    failed = 0

    for f in files:
        filepath = os.path.join(voice_dir, f)
        old_size = os.path.getsize(filepath)

        if trim_end(filepath, trim_ms):
            new_size = os.path.getsize(filepath)
            saved = old_size - new_size
            print(f"    {f}: {old_size // 1024}KB -> {new_size // 1024}KB (-{saved // 1024}KB)")
            success += 1
        else:
            print(f"    {f}: FALHOU")
            failed += 1

    return success, failed


def main():
    parser = argparse.ArgumentParser(description="Remove 200ms from END of context WAVs")
    parser.add_argument("--voice", choices=VOICES, help="Single voice to process")
    parser.add_argument("--ms", type=int, default=200, help="Milliseconds to trim from end (default: 200)")
    args = parser.parse_args()

    voices = [args.voice] if args.voice else VOICES

    print("=" * 55)
    print("  GESTUUM — Remover 200ms do FINAL dos contextos")
    print("  Apenas arquivos ctx_* (mao esquerda)")
    print("=" * 55)
    print()

    total_success = 0
    total_failed = 0

    for voice in voices:
        print(f"  [{voice}]")
        s, f = process_voice(voice, args.ms)
        total_success += s
        total_failed += f
        print()

    print("=" * 55)
    print(f"  Resultado: {total_success} OK, {total_failed} falhas")
    print("=" * 55)


if __name__ == "__main__":
    main()
