"""
GESTUUM — Generate all audio files using Google Cloud TTS
Generates 108 WAVs per voice × 4 voices = 432 total.

Setup:
    set GOOGLE_TTS_API_KEY=AIzaSy...
    python generate_google_tts.py

Or:
    python generate_google_tts.py --key AIzaSy...
    python generate_google_tts.py --key AIzaSy... --voice menino
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
# FIX: Paths encurtados para caber no SPIFFS (max 31 chars por path)
# Mapeamento: homem→h, mulher→m, menino→n, menina→i
AUDIO_DIR = os.path.join(PROJECT_ROOT, "sensor_a", "data", "a")

# Mapeamento de nomes longos para codigos curtos de pasta
VOICE_DIR_MAP = {"homem": "h", "mulher": "m", "menino": "n", "menina": "i"}

TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"

# === 4 VOICES (approved by user) ===
VOICES = {
    "homem": {
        "voice_id": "pt-BR-Neural2-B",
        "gender": "MALE",
        "pitch": 0.0,
        "rate": 0.9,
        "label": "Homem (Neural2-B)",
    },
    "mulher": {
        "voice_id": "pt-BR-Wavenet-A",
        "gender": "FEMALE",
        "pitch": 0.0,
        "rate": 0.9,
        "label": "Mulher (Wavenet-A)",
    },
    "menino": {
        "voice_id": "pt-BR-Wavenet-B",
        "gender": "MALE",
        "pitch": 6.0,
        "rate": 0.9,
        "label": "Menino (Wavenet-B +6st)",
    },
    "menina": {
        "voice_id": "pt-BR-Neural2-C",
        "gender": "FEMALE",
        "pitch": 6.0,
        "rate": 0.9,
        "label": "Menina (Neural2-C +6st)",
    },
}

# === Gender-specific word overrides (female voices) ===
FEMALE_OVERRIDES = {
    "obrigado": "Obrigada",
    "tonto": "Tonta",
}

# === 88 OBJECT GESTURES (right hand) ===
OBJECTS = [
    # BASE — GERAL (10)
    ("agua", "Água"), ("comida", "Comida"), ("banheiro", "Banheiro"),
    ("remedio", "Remédio"), ("descansar", "Descansar"), ("dormir", "Dormir"),
    ("bem", "Bem"), ("mal", "Mal"), ("fome", "Fome"), ("sede", "Sede"),
    # BASE — EMERGENCIA (10)
    ("ajuda", "Ajuda"), ("socorro", "Socorro!"), ("medico", "Médico"),
    ("dor", "Dor"), ("muita_dor", "Muita dor!"), ("tonto", "Tonto"),
    ("cabeca", "Cabeça"), ("peito", "Peito"),
    ("nao_respiro", "Não consigo respirar!"), ("ambulancia", "Ambulância!"),
    # BASE — CASA (10)
    ("quarto", "Quarto"), ("sala", "Sala"), ("cozinha", "Cozinha"),
    ("luz", "Luz"), ("tv", "Televisão"), ("ar", "Ar condicionado"),
    ("ligar", "Ligar"), ("desligar", "Desligar"), ("porta", "Porta"),
    ("janela", "Janela"),
    # BASE — TRABALHO (10)
    ("reuniao", "Reunião"), ("escritorio", "Escritório"),
    ("computador", "Computador"), ("documento", "Documento"),
    ("email", "E-mail"), ("cafe", "Café"), ("pausa", "Pausa"),
    ("comecar", "Começar"), ("terminar", "Terminar"),
    ("ajuda_trab", "Preciso de ajuda"),
    # BASE — SOCIAL (10)
    ("oi", "Oi!"), ("tchau", "Tchau!"), ("bom_dia", "Bom dia!"),
    ("obrigado", "Obrigado"), ("por_favor", "Por favor"), ("sim", "Sim"),
    ("nao", "Não"), ("ok", "Ok"), ("feliz", "Feliz"), ("triste", "Triste"),
    # HOSPITAL (13)
    ("enfermeira", "Enfermeira"), ("medico_plantao", "Médico de plantão"),
    ("soro", "Soro"), ("exame", "Exame"), ("visita", "Visita"),
    ("alta", "Alta"), ("posicao_cama", "Mudar posição da cama"),
    ("cobertor", "Cobertor"), ("leito", "Leito"), ("curativo", "Curativo"),
    ("alergia_hosp", "Tenho alergia"), ("nausea", "Náusea"), ("dieta", "Dieta"),
    # ESCOLA (13)
    ("professor", "Professor"), ("aula", "Aula"), ("intervalo", "Intervalo"),
    ("licao", "Lição"), ("brincar", "Brincar"), ("colega", "Colega"),
    ("mochila", "Mochila"), ("quadra", "Quadra"), ("cantina", "Cantina"),
    ("diretor", "Diretor"), ("alergia_escola", "Tenho alergia alimentar"),
    ("lanche", "Lanche"), ("prova", "Prova"),
    # TRANSPORTE (12)
    ("motorista", "Motorista"), ("parada", "Parada"), ("destino", "Destino"),
    ("endereco", "Endereço"), ("mapa", "Mapa"), ("chegou", "Chegou"),
    ("esperar", "Esperar"), ("bilhete", "Bilhete"), ("aeroporto", "Aeroporto"),
    ("hotel", "Hotel"), ("taxi", "Táxi"), ("onibus", "Ônibus"),
]

# === 20 CONTEXT GESTURES (left hand) ===
CONTEXTS = [
    ("ctx_e01_preciso_de", "Preciso de"),
    ("ctx_e02_quero", "Quero"),
    ("ctx_e03_posso", "Posso"),
    ("ctx_e04_vou", "Vou"),
    ("ctx_e05_estou", "Estou"),
    ("ctx_e06_estou_com", "Estou com"),
    ("ctx_e07_nao_preciso_de", "Não preciso de"),
    ("ctx_e08_nao_quero", "Não quero"),
    ("ctx_e09_nao_posso", "Não posso"),
    ("ctx_e10_nao_vou", "Não vou"),
    ("ctx_e11_nao_estou", "Não estou"),
    ("ctx_e12_nao_estou_com", "Não estou com"),
    ("ctx_e13_onde_esta", "Onde está"),
    ("ctx_e14_quando", "Quando"),
    ("ctx_e15_quem", "Quem"),
    ("ctx_e16_como", "Como"),
    ("ctx_e17_o_que_e", "O que é"),
    ("ctx_e18_por_que", "Por que"),
    ("ctx_e19_chame", "Chame"),
    ("ctx_e20_avise", "Avise"),
]


def synthesize(api_key, voice_id, gender, text, pitch=0.0, rate=0.9, sample_rate=22050):
    """Call Google Cloud TTS API, return WAV bytes."""
    body = {
        "input": {"text": text},
        "voice": {
            "languageCode": "pt-BR",
            "name": voice_id,
            "ssmlGender": gender,
        },
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": sample_rate,
            "pitch": pitch,
            "speakingRate": rate,
        },
    }

    req = urllib.request.Request(
        f"{TTS_URL}?key={api_key}",
        data=json.dumps(body).encode("utf-8"),
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    resp = urllib.request.urlopen(req, timeout=30)
    result = json.loads(resp.read().decode("utf-8"))
    return base64.b64decode(result.get("audioContent", ""))


def add_pre_silence(audio_bytes, silence_ms, sample_rate):
    """Add silence at the beginning of WAV audio."""
    silence_samples = int(sample_rate * silence_ms / 1000)
    silence = b'\x00\x00' * silence_samples

    if len(audio_bytes) > 44:
        header = bytearray(audio_bytes[:44])
        data = silence + audio_bytes[44:]
        struct.pack_into('<I', header, 4, len(data) + 36)
        struct.pack_into('<I', header, 40, len(data))
        return bytes(header) + data
    return audio_bytes


def generate_voice(api_key, voice_key, voice_cfg):
    """Generate all 108 audio files for one voice."""
    # Usa codigo curto da pasta (h, m, n, i) para caber no SPIFFS
    voice_dir_code = VOICE_DIR_MAP.get(voice_key, voice_key)
    voice_dir = os.path.join(AUDIO_DIR, voice_dir_code)
    os.makedirs(voice_dir, exist_ok=True)

    is_female = voice_key in ("mulher", "menina")
    voice_id = voice_cfg["voice_id"]
    gender = voice_cfg["gender"]
    pitch = voice_cfg["pitch"]
    rate = voice_cfg["rate"]

    success = 0
    failed = 0
    total_size = 0

    # Objects (88) — 22050Hz, 200ms pre-silence
    print(f"\n  Objetos (88):")
    for i, (filename, text) in enumerate(OBJECTS, 1):
        spoken = FEMALE_OVERRIDES.get(filename, text) if is_female else text
        wav_path = os.path.join(voice_dir, f"{filename}.wav")

        print(f"    [{i:2d}/88] {filename}.wav", end=" ... ", flush=True)

        try:
            audio = synthesize(api_key, voice_id, gender, spoken, pitch, rate, 22050)
            audio = add_pre_silence(audio, 200, 22050)
            with open(wav_path, "wb") as f:
                f.write(audio)
            size = len(audio)
            total_size += size
            print(f"OK ({size // 1024} KB)")
            success += 1
        except Exception as e:
            print(f"FALHOU ({e})")
            failed += 1

        # Rate limiting: ~10 requests/sec to stay within quota
        time.sleep(0.1)

    # Contexts (20) — 22050Hz, 200ms pre-silence, NO post-silence (trimmed)
    print(f"\n  Contextos (20):")
    for i, (filename, text) in enumerate(CONTEXTS, 1):
        wav_path = os.path.join(voice_dir, f"{filename}.wav")

        print(f"    [{i:2d}/20] {filename}.wav", end=" ... ", flush=True)

        try:
            audio = synthesize(api_key, voice_id, gender, text, pitch, rate, 22050)
            audio = add_pre_silence(audio, 200, 22050)
            with open(wav_path, "wb") as f:
                f.write(audio)
            size = len(audio)
            total_size += size
            print(f"OK ({size // 1024} KB)")
            success += 1
        except Exception as e:
            print(f"FALHOU ({e})")
            failed += 1

        time.sleep(0.1)

    return success, failed, total_size


def main():
    parser = argparse.ArgumentParser(description="GESTUUM — Generate audio with Google Cloud TTS")
    parser.add_argument("--key", help="Google Cloud API key")
    parser.add_argument("--voice", choices=list(VOICES.keys()), help="Single voice")
    args = parser.parse_args()

    api_key = args.key or os.environ.get("GOOGLE_TTS_API_KEY")
    if not api_key:
        print("ERRO: API key necessária.")
        print("  python generate_google_tts.py --key SUA_CHAVE")
        sys.exit(1)

    voices_to_gen = {args.voice: VOICES[args.voice]} if args.voice else VOICES

    print("=" * 60)
    print("  GESTUUM — Google Cloud TTS Audio Generator")
    print("=" * 60)
    print(f"  Vozes: {', '.join(voices_to_gen.keys())}")
    print(f"  Objetos: {len(OBJECTS)} por voz")
    print(f"  Contextos: {len(CONTEXTS)} por voz")
    print(f"  Total: {(len(OBJECTS) + len(CONTEXTS)) * len(voices_to_gen)} arquivos")
    print(f"  Formato: 22050Hz, 16-bit, mono WAV")
    print(f"  Output: {AUDIO_DIR}")

    grand_success = 0
    grand_failed = 0
    grand_size = 0

    for voice_key, voice_cfg in voices_to_gen.items():
        print(f"\n{'='*60}")
        print(f"  {voice_cfg['label']}")
        print(f"  Voice: {voice_cfg['voice_id']} | Pitch: {voice_cfg['pitch']} | Rate: {voice_cfg['rate']}")
        print(f"{'='*60}")

        s, f, sz = generate_voice(api_key, voice_key, voice_cfg)
        grand_success += s
        grand_failed += f
        grand_size += sz

        print(f"\n  {voice_key}: {s} OK, {f} falhas ({sz // 1024} KB)")

    print(f"\n{'='*60}")
    print(f"  RESULTADO FINAL")
    print(f"  {grand_success} OK, {grand_failed} falhas")
    print(f"  Tamanho total: {grand_size // 1024} KB ({grand_size / 1024 / 1024:.1f} MB)")

    for vk in voices_to_gen:
        vdir = os.path.join(AUDIO_DIR, VOICE_DIR_MAP.get(vk, vk))
        count = len([f for f in os.listdir(vdir) if f.endswith('.wav')])
        import subprocess
        size = sum(os.path.getsize(os.path.join(vdir, f)) for f in os.listdir(vdir) if f.endswith('.wav'))
        print(f"  {vk}: {count} arquivos ({size // 1024} KB)")

    print(f"\n  SPIFFS: 6.2 MB")
    print(f"  Maior voz cabe? Verificar acima.")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
