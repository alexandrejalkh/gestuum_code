"""
GESTUUM — Test Google Cloud TTS for child voices
Tests all available PT-BR voices with different pitch levels.

Setup:
    1. Enable Text-to-Speech API: https://console.cloud.google.com/apis/library/texttospeech.googleapis.com
    2. Create API key: https://console.cloud.google.com/apis/credentials
    3. Run: python test_google_tts.py --key YOUR_API_KEY

Or set environment variable:
    set GOOGLE_TTS_API_KEY=YOUR_API_KEY
    python test_google_tts.py
"""

import argparse
import base64
import json
import os
import struct
import sys
import urllib.request
import urllib.error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.join(SCRIPT_DIR, "_test_google_tts")

# Google Cloud TTS REST endpoint
TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"

# All PT-BR voices available
VOICES = [
    ("pt-BR-Standard-A", "FEMALE", "Standard A (feminina)"),
    ("pt-BR-Standard-B", "MALE", "Standard B (masculina)"),
    ("pt-BR-Standard-C", "FEMALE", "Standard C (feminina alt)"),
    ("pt-BR-Wavenet-A", "FEMALE", "Wavenet A (feminina, melhor qualidade)"),
    ("pt-BR-Wavenet-B", "MALE", "Wavenet B (masculina, melhor qualidade)"),
    ("pt-BR-Wavenet-C", "FEMALE", "Wavenet C (feminina alt, melhor qualidade)"),
    ("pt-BR-Neural2-A", "FEMALE", "Neural2 A (feminina, melhor)"),
    ("pt-BR-Neural2-B", "MALE", "Neural2 B (masculina, melhor)"),
    ("pt-BR-Neural2-C", "FEMALE", "Neural2 C (feminina alt, melhor)"),
]

# Pitch variations for child voices (semitones, -20.0 to +20.0)
PITCH_TESTS = [
    (0.0, "normal"),
    (4.0, "+4st (jovem)"),
    (6.0, "+6st (crianca)"),
    (8.0, "+8st (crianca aguda)"),
    (10.0, "+10st (muito aguda)"),
]

TEST_TEXT = "Não quero remédio."
TEST_TEXT_2 = "Estou com fome."


def synthesize(api_key, voice_name, gender, text, pitch=0.0, speaking_rate=1.0):
    """Call Google Cloud TTS API and return audio bytes (LINEAR16 WAV)."""
    body = {
        "input": {"text": text},
        "voice": {
            "languageCode": "pt-BR",
            "name": voice_name,
            "ssmlGender": gender,
        },
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": 11025,
            "pitch": pitch,
            "speakingRate": speaking_rate,
        },
    }

    req = urllib.request.Request(
        f"{TTS_URL}?key={api_key}",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    try:
        resp = urllib.request.urlopen(req, timeout=30)
        result = json.loads(resp.read().decode("utf-8"))
        audio_b64 = result.get("audioContent", "")
        return base64.b64decode(audio_b64)
    except urllib.error.HTTPError as e:
        error_body = e.read().decode("utf-8") if e.fp else ""
        print(f"    HTTP {e.code}: {error_body[:200]}")
        return None
    except Exception as e:
        print(f"    Error: {e}")
        return None


def add_wav_silence(audio_bytes, silence_ms=200, sample_rate=11025):
    """Add silence at the beginning of WAV audio data."""
    silence_samples = int(sample_rate * silence_ms / 1000)
    silence_bytes = b'\x00\x00' * silence_samples  # 16-bit silence

    # Google returns raw WAV with header
    # Insert silence after the 44-byte WAV header
    if len(audio_bytes) > 44:
        header = bytearray(audio_bytes[:44])
        data = audio_bytes[44:]
        new_data = silence_bytes + data

        # Update header sizes
        data_size = len(new_data)
        file_size = data_size + 36
        struct.pack_into('<I', header, 4, file_size)   # RIFF chunk size
        struct.pack_into('<I', header, 40, data_size)   # data chunk size

        return bytes(header) + new_data
    return audio_bytes


def main():
    parser = argparse.ArgumentParser(description="Test Google Cloud TTS for GESTUUM")
    parser.add_argument("--key", help="Google Cloud API key")
    parser.add_argument("--voice", help="Specific voice to test (e.g., pt-BR-Neural2-B)")
    parser.add_argument("--all-pitches", action="store_true", help="Test all pitch levels for each voice")
    args = parser.parse_args()

    api_key = args.key or os.environ.get("GOOGLE_TTS_API_KEY")
    if not api_key:
        print("ERRO: API key necessaria.")
        print("  python test_google_tts.py --key SUA_CHAVE")
        print("  ou: set GOOGLE_TTS_API_KEY=SUA_CHAVE")
        print()
        print("Para obter a chave:")
        print("  1. https://console.cloud.google.com/apis/library/texttospeech.googleapis.com")
        print("  2. Ativar a API")
        print("  3. https://console.cloud.google.com/apis/credentials")
        print("  4. Criar credenciais -> Chave de API")
        sys.exit(1)

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    print()
    print("=" * 60)
    print("  GESTUUM — Teste Google Cloud TTS")
    print("  Vozes PT-BR com variações de pitch para crianças")
    print("=" * 60)
    print()

    voices_to_test = VOICES
    if args.voice:
        voices_to_test = [(v, g, d) for v, g, d in VOICES if v == args.voice]
        if not voices_to_test:
            print(f"Voz '{args.voice}' nao encontrada. Disponiveis:")
            for v, g, d in VOICES:
                print(f"  {v} ({d})")
            sys.exit(1)

    pitches = PITCH_TESTS if args.all_pitches else [(0.0, "normal"), (6.0, "+6st (crianca)")]
    total = 0
    ok = 0

    for voice_name, gender, desc in voices_to_test:
        print(f"--- {voice_name} ({desc}) ---")

        for pitch, pitch_desc in pitches:
            filename = f"{voice_name}_pitch{pitch:+.0f}.wav"
            filepath = os.path.join(OUTPUT_DIR, filename)
            total += 1

            print(f"  pitch {pitch_desc}", end=" ... ")

            audio = synthesize(api_key, voice_name, gender, TEST_TEXT, pitch=pitch, speaking_rate=0.9)
            if audio:
                # Add 200ms pre-silence for HAT-SPK2
                audio = add_wav_silence(audio, 200)
                with open(filepath, "wb") as f:
                    f.write(audio)
                size = len(audio) // 1024
                print(f"OK ({size} KB) -> {filename}")
                ok += 1
            else:
                print("FALHOU")

        print()

    print("=" * 60)
    print(f"  Resultado: {ok}/{total} OK")
    print(f"  Arquivos em: {OUTPUT_DIR}")
    print()
    print("  Vozes para ouvir e comparar:")
    print("  - *_pitch+0.wav  = voz adulta normal")
    print("  - *_pitch+6.wav  = voz de crianca (+6 semitons)")
    if args.all_pitches:
        print("  - *_pitch+4.wav  = voz jovem")
        print("  - *_pitch+8.wav  = crianca mais aguda")
        print("  - *_pitch+10.wav = muito aguda")
    print()
    print("  Recomendacao: Neural2 ou Wavenet tem melhor qualidade")
    print("=" * 60)


if __name__ == "__main__":
    main()
