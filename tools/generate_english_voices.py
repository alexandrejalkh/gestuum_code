"""
GESTUUM — Gerar WAVs em ingles para as 3 vozes faltantes (mulher, menino, menina).
A voz masculina (h) ja foi gerada anteriormente com en-US-Neural2-D.

Uso:
    set GOOGLE_TTS_API_KEY=AIzaSy...
    python generate_english_voices.py
    python generate_english_voices.py --key AIzaSy... --voice mulher
    python generate_english_voices.py --key AIzaSy... --voice all
"""

import argparse
import base64
import json
import os
import struct
import sys
import time
import urllib.request
import urllib.error

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
AUDIO_DIR = os.path.join(PROJECT_ROOT, "sensor_a", "data", "a")

# Mapeamento de nomes para pastas SPIFFS
VOICE_DIR_MAP = {"homem": "h", "mulher": "m", "menino": "n", "menina": "i"}

TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"

# === Vozes EN para cada perfil ===
# Homem ja gerado (en-US-Neural2-D). Mulher, menino e menina pendentes.
VOICES = {
    "homem": {
        "voice_id": "en-US-Neural2-D",
        "gender": "MALE",
        "pitch": 0.0,
        "rate": 1.0,
        "label": "Man (Neural2-D)",
    },
    "mulher": {
        "voice_id": "en-US-Neural2-F",
        "gender": "FEMALE",
        "pitch": 0.0,
        "rate": 1.0,
        "label": "Woman (Neural2-F)",
    },
    "menino": {
        "voice_id": "en-US-Neural2-D",
        "gender": "MALE",
        "pitch": 6.0,
        "rate": 1.0,
        "label": "Boy (Neural2-D +6st)",
    },
    "menina": {
        "voice_id": "en-US-Neural2-F",
        "gender": "FEMALE",
        "pitch": 6.0,
        "rate": 1.0,
        "label": "Girl (Neural2-F +6st)",
    },
}

# === 43 WAVs do vocabulario ingles ===
# (filename_sem_extensao, texto_para_falar)

# Contextos — mao esquerda (8)
CONTEXTS = [
    ("ctx_01_i_want", "I want"),
    ("ctx_02_i_dont_want", "I don't want"),
    ("ctx_03_i_need", "I need"),
    ("ctx_04_i_feel", "I feel"),
    ("ctx_05_where_is", "Where is"),
    ("ctx_06_call", "Call"),
    ("ctx_07_i_am", "I am"),
    ("ctx_08_i_am_not", "I am not"),
]

# Objetos — mao direita (25)
OBJECTS = [
    ("water", "water"),
    ("food", "food"),
    ("bathroom", "bathroom"),
    ("medicine", "medicine"),
    ("rest", "rest"),
    ("pain", "pain"),
    ("head", "head"),
    ("chest", "chest"),
    ("hungry", "hungry"),
    ("thirsty", "thirsty"),
    ("room", "room"),
    ("living_room", "living room"),
    ("light", "light"),
    ("door", "door"),
    ("window", "window"),
    ("doctor", "doctor"),
    ("nurse", "nurse"),
    ("teacher", "teacher"),
    ("sleep", "sleep"),
    ("eat", "eat"),
    ("ambulance", "ambulance"),
    ("cant_breathe", "can't breathe"),
    ("class", "class"),
    ("break_time", "break time"),
    ("snack", "snack"),
]

# Solo — sem composicao (10)
SOLOS = [
    ("help", "Help!"),
    ("yes", "Yes"),
    ("no", "No"),
    ("hi", "Hi!"),
    ("bye", "Bye!"),
    ("thank_you", "Thank you"),
    ("please", "Please"),
    ("good_morning", "Good morning"),
    ("ok", "OK"),
    ("i_need_help", "I need help!"),
]

ALL_WORDS = CONTEXTS + OBJECTS + SOLOS


def normalize_rms(samples, target_rms=11000):
    """Normaliza amplitude pelo RMS para volume perceptivo consistente."""
    if not samples:
        return samples
    # Calcular RMS atual
    sum_sq = sum(s * s for s in samples)
    rms = (sum_sq / len(samples)) ** 0.5
    if rms < 1.0:
        return samples
    # Calcular ganho necessario
    gain = target_rms / rms
    # Aplicar ganho com clipping em 16-bit
    result = []
    for s in samples:
        v = int(s * gain)
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        result.append(v)
    return result


def make_wav(samples, sample_rate=48000):
    """Cria WAV 16-bit mono PCM a partir de lista de samples int16."""
    num_samples = len(samples)
    data_size = num_samples * 2
    # Header WAV (44 bytes)
    header = struct.pack('<4sI4s', b'RIFF', 36 + data_size, b'WAVE')
    header += struct.pack('<4sIHHIIHH', b'fmt ', 16, 1, 1,
                          sample_rate, sample_rate * 2, 2, 16)
    header += struct.pack('<4sI', b'data', data_size)
    # Dados PCM
    pcm = struct.pack(f'<{num_samples}h', *samples)
    return header + pcm


def call_tts(text, voice_cfg, api_key):
    """Chama Google Cloud TTS e retorna audio PCM como lista de int16."""
    body = {
        "input": {"text": text},
        "voice": {
            "languageCode": "en-US",
            "name": voice_cfg["voice_id"],
            "ssmlGender": voice_cfg["gender"],
        },
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": 48000,
            "speakingRate": voice_cfg["rate"],
            "pitch": voice_cfg["pitch"],
            "effectsProfileId": ["small-bluetooth-speaker-class-device"],
        },
    }
    data = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(
        f"{TTS_URL}?key={api_key}",
        data=data,
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        result = json.loads(resp.read().decode("utf-8"))

    audio_bytes = base64.b64decode(result["audioContent"])
    # Pular header WAV (44 bytes) — pegar samples PCM 16-bit
    pcm_data = audio_bytes[44:]
    num_samples = len(pcm_data) // 2
    samples = list(struct.unpack(f'<{num_samples}h', pcm_data))
    return samples


def generate_voice(voice_name, api_key):
    """Gera todos os 43 WAVs para uma voz."""
    voice_cfg = VOICES[voice_name]
    voice_dir = VOICE_DIR_MAP[voice_name]
    out_dir = os.path.join(AUDIO_DIR, voice_dir)
    os.makedirs(out_dir, exist_ok=True)

    print(f"\n{'='*60}")
    print(f"Gerando voz: {voice_cfg['label']} -> {out_dir}")
    print(f"{'='*60}")

    total = len(ALL_WORDS)
    ok = 0
    errors = 0

    for i, (filename, text) in enumerate(ALL_WORDS, 1):
        wav_path = os.path.join(out_dir, f"{filename}.wav")
        print(f"  [{i:2d}/{total}] {filename}.wav — \"{text}\"... ", end="", flush=True)

        try:
            # Chamar Google TTS
            samples = call_tts(text, voice_cfg, api_key)
            # Normalizar RMS para volume consistente
            samples = normalize_rms(samples, target_rms=11000)
            # Criar WAV
            wav_data = make_wav(samples, 48000)
            # Salvar
            with open(wav_path, "wb") as f:
                f.write(wav_data)

            size_kb = len(wav_data) / 1024
            print(f"OK ({size_kb:.1f} KB, {len(samples)} samples)")
            ok += 1
        except urllib.error.HTTPError as e:
            print(f"ERRO HTTP {e.code}: {e.read().decode('utf-8', errors='replace')[:100]}")
            errors += 1
        except Exception as e:
            print(f"ERRO: {e}")
            errors += 1

        # Rate limiting — Google TTS tem limite de requests/min
        time.sleep(0.3)

    print(f"\nResultado: {ok}/{total} OK, {errors} erros")
    return ok, errors


def main():
    parser = argparse.ArgumentParser(description="Gerar WAVs em ingles para GESTUUM")
    parser.add_argument("--key", help="Google TTS API key")
    parser.add_argument("--voice", default="all",
                        choices=["mulher", "menino", "menina", "homem", "all"],
                        help="Qual voz gerar (default: all = mulher+menino+menina)")
    args = parser.parse_args()

    api_key = args.key or os.environ.get("GOOGLE_TTS_API_KEY")
    if not api_key:
        print("ERRO: API key necessaria. Use --key ou set GOOGLE_TTS_API_KEY")
        sys.exit(1)

    if args.voice == "all":
        # Gerar as 3 vozes faltantes (homem ja existe)
        voices_to_gen = ["mulher", "menino", "menina"]
    else:
        voices_to_gen = [args.voice]

    total_ok = 0
    total_errors = 0

    for voice in voices_to_gen:
        ok, errors = generate_voice(voice, api_key)
        total_ok += ok
        total_errors += errors

    print(f"\n{'='*60}")
    print(f"TOTAL: {total_ok} WAVs gerados, {total_errors} erros")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
