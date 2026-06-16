"""
GESTUUM — Remove 200ms pre-silence from object WAVs (right hand)
Trims the leading silence that was added for HAT-SPK2 init delay.
Only affects object files (not ctx_* context files).

Usage:
    python trim_silence.py                  # all 4 voices
    python trim_silence.py --voice homem    # single voice
"""

import os
import subprocess
import shutil
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
AUDIO_BASE = os.path.join(os.path.dirname(SCRIPT_DIR), "sensor_a", "data", "audio")

VOICES = ["homem", "mulher", "menino", "menina"]

# Find ffmpeg
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


def trim_file(wav_path, trim_ms=200):
    """Remove first trim_ms milliseconds from a WAV file."""
    directory = os.path.dirname(wav_path)
    basename = os.path.basename(wav_path)
    tmp_path = os.path.join(directory, "_tmp_" + basename)
    trim_sec = trim_ms / 1000.0

    cmd = [
        FFMPEG, "-y",
        "-i", wav_path,
        "-ss", str(trim_sec),
        "-c:a", "pcm_s16le",
        "-ar", "11025",
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
        print(f"      ffmpeg error: {result.stderr[-200:] if result.stderr else 'unknown'}")
        return False


def process_voice(voice):
    voice_dir = os.path.join(AUDIO_BASE, voice)
    if not os.path.isdir(voice_dir):
        print(f"  {voice}: pasta nao encontrada")
        return 0, 0

    files = sorted([f for f in os.listdir(voice_dir)
                    if f.endswith(".wav") and not f.startswith("ctx_")])

    success = 0
    failed = 0

    for f in files:
        filepath = os.path.join(voice_dir, f)
        old_size = os.path.getsize(filepath)

        if trim_file(filepath, 200):
            new_size = os.path.getsize(filepath)
            saved = old_size - new_size
            print(f"    {f}: {old_size // 1024}KB -> {new_size // 1024}KB (-{saved // 1024}KB)")
            success += 1
        else:
            print(f"    {f}: FALHOU")
            failed += 1

    return success, failed


def main():
    parser = argparse.ArgumentParser(description="Remove 200ms pre-silence from object WAVs")
    parser.add_argument("--voice", choices=VOICES, help="Single voice to process")
    args = parser.parse_args()

    voices = [args.voice] if args.voice else VOICES

    print("=" * 50)
    print("  GESTUUM — Remover 200ms de silencio inicial")
    print("  Apenas arquivos de objetos (nao ctx_*)")
    print("=" * 50)
    print()

    total_success = 0
    total_failed = 0

    for voice in voices:
        print(f"  [{voice}]")
        s, f = process_voice(voice)
        total_success += s
        total_failed += f
        print()

    print("=" * 50)
    print(f"  Resultado: {total_success} OK, {total_failed} falhas")
    print("=" * 50)


if __name__ == "__main__":
    main()
