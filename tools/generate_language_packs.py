"""
GESTUUM — Gerar packs de idiomas (PT-BR e ES) com mesmos nomes de arquivo.
O pack EN ja existe. Mesmos 43 filenames, texto diferente por idioma.

Uso:
    python generate_language_packs.py --key AIzaSy... --lang pt
    python generate_language_packs.py --key AIzaSy... --lang es
    python generate_language_packs.py --key AIzaSy... --lang all
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
TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"

# === Configuracao de voz por idioma ===
LANG_CONFIG = {
    "pt": {
        "voice_id": "pt-BR-Neural2-B",
        "lang_code": "pt-BR",
        "label": "Portugues BR (Neural2-B)",
        "folder": "_voices_pt",
    },
    "es": {
        "voice_id": "es-US-Neural2-B",
        "lang_code": "es-US",
        "label": "Espanhol US (Neural2-B)",
        "folder": "_voices_es",
    },
    "fr": {
        "voice_id": "fr-FR-Neural2-B",
        "lang_code": "fr-FR",
        "label": "Frances FR (Neural2-B)",
        "folder": "_voices_fr",
    },
    "zh": {
        "voice_id": "cmn-CN-Wavenet-B",
        "lang_code": "cmn-CN",
        "label": "Mandarim CN (Wavenet-B)",
        "folder": "_voices_zh",
    },
    "ar": {
        "voice_id": "ar-XA-Wavenet-B",
        "lang_code": "ar-XA",
        "label": "Arabe (Wavenet-B)",
        "folder": "_voices_ar",
    },
}

# === 43 palavras — mesmo filename, texto diferente por idioma ===
# (filename, en, pt, es, fr, zh, ar)
WORDS = [
    # Contextos (8)
    ("ctx_01_i_want",       "I want",           "Eu quero",             "Yo quiero",            "Je veux",              "我想要",        "أريد"),
    ("ctx_02_i_dont_want",  "I don't want",     "Eu nao quero",         "Yo no quiero",         "Je ne veux pas",       "我不想要",      "لا أريد"),
    ("ctx_03_i_need",       "I need",            "Eu preciso",           "Yo necesito",          "J'ai besoin",          "我需要",        "أحتاج"),
    ("ctx_04_i_feel",       "I feel",            "Eu sinto",             "Yo siento",            "Je ressens",           "我感觉",        "أشعر"),
    ("ctx_05_where_is",     "Where is",          "Onde esta",            "Donde esta",           "Ou est",               "在哪里",        "أين"),
    ("ctx_06_call",         "Call",              "Chame",                "Llama",                "Appelez",              "叫",            "اتصل"),
    ("ctx_07_i_am",         "I am",              "Eu estou",             "Yo estoy",             "Je suis",              "我是",          "أنا"),
    ("ctx_08_i_am_not",     "I am not",          "Eu nao estou",         "Yo no estoy",          "Je ne suis pas",       "我不是",        "أنا لست"),
    # Objetos (25)
    ("water",        "water",             "agua",                "agua",                 "de l'eau",             "水",            "ماء"),
    ("food",         "food",              "comida",              "comida",               "a manger",             "食物",          "طعام"),
    ("bathroom",     "bathroom",          "banheiro",            "el banho",             "les toilettes",        "卫生间",        "الحمام"),
    ("medicine",     "medicine",          "remedio",             "medicina",             "un medicament",        "药",            "دواء"),
    ("rest",         "rest",              "descansar",           "descanso",             "me reposer",          "休息",          "راحة"),
    ("pain",         "pain",              "dor",                 "dolor",                "mal",                  "疼痛",          "ألم"),
    ("head",         "head",              "cabeca",              "cabeza",               "la tete",              "头",            "رأس"),
    ("chest",        "chest",             "peito",               "pecho",                "la poitrine",          "胸口",          "صدر"),
    ("hungry",       "hungry",            "fome",                "hambre",               "faim",                 "饿",            "جائع"),
    ("thirsty",      "thirsty",           "sede",                "sed",                  "soif",                 "渴",            "عطشان"),
    ("room",         "room",              "quarto",              "habitacion",           "la chambre",           "房间",          "غرفة"),
    ("living_room",  "living room",       "sala",                "sala",                 "le salon",             "客厅",          "غرفة المعيشة"),
    ("light",        "light",             "luz",                 "luz",                  "la lumiere",           "灯",            "ضوء"),
    ("door",         "door",              "porta",               "puerta",               "la porte",             "门",            "باب"),
    ("window",       "window",            "janela",              "ventana",              "la fenetre",           "窗户",          "نافذة"),
    ("doctor",       "doctor",            "medico",              "doctor",               "le medecin",           "医生",          "طبيب"),
    ("nurse",        "nurse",             "enfermeira",          "enfermera",            "l'infirmiere",         "护士",          "ممرضة"),
    ("teacher",      "teacher",           "professor",           "profesor",             "le professeur",        "老师",          "معلم"),
    ("sleep",        "sleep",             "dormir",              "dormir",               "dormir",               "睡觉",          "نوم"),
    ("eat",          "eat",               "comer",               "comer",                "manger",               "吃",            "أكل"),
    ("ambulance",    "ambulance",         "ambulancia",          "ambulancia",           "une ambulance",        "救护车",        "سيارة إسعاف"),
    ("cant_breathe", "can't breathe",     "nao consigo respirar","no puedo respirar",    "je ne peux pas respirer","我无法呼吸",  "لا أستطيع التنفس"),
    ("class",        "class",             "aula",                "clase",                "la classe",            "课堂",          "فصل"),
    ("break_time",   "break time",        "intervalo",           "recreo",               "la pause",             "休息时间",      "استراحة"),
    ("snack",        "snack",             "lanche",              "merienda",             "un gouter",            "零食",          "وجبة خفيفة"),
    # Solo (10)
    ("help",         "Help!",             "Socorro!",            "Ayuda!",               "Au secours!",          "救命!",         "!مساعدة"),
    ("yes",          "Yes",               "Sim",                 "Si",                   "Oui",                  "是的",          "نعم"),
    ("no",           "No",                "Nao",                 "No",                   "Non",                  "不",            "لا"),
    ("hi",           "Hi!",               "Oi!",                 "Hola!",                "Bonjour!",             "你好!",         "!مرحبا"),
    ("bye",          "Bye!",              "Tchau!",              "Adios!",               "Au revoir!",           "再见!",         "!مع السلامة"),
    ("thank_you",    "Thank you",         "Obrigado",            "Gracias",              "Merci",                "谢谢",          "شكرا"),
    ("please",       "Please",            "Por favor",           "Por favor",            "S'il vous plait",      "请",            "من فضلك"),
    ("good_morning", "Good morning",      "Bom dia",             "Buenos dias",          "Bonjour",              "早上好",        "صباح الخير"),
    ("ok",           "OK",                "OK",                  "OK",                   "D'accord",             "好的",          "حسنا"),
    ("i_need_help",  "I need help!",      "Preciso de ajuda!",   "Necesito ayuda!",      "J'ai besoin d'aide!",  "我需要帮助!",   "!أحتاج مساعدة"),
]

LANG_INDEX = {"en": 1, "pt": 2, "es": 3, "fr": 4, "zh": 5, "ar": 6}


def normalize_rms(samples, target_rms=11000):
    if not samples:
        return samples
    sum_sq = sum(s * s for s in samples)
    rms = (sum_sq / len(samples)) ** 0.5
    if rms < 1.0:
        return samples
    gain = target_rms / rms
    return [max(-32768, min(32767, int(s * gain))) for s in samples]


def make_wav(samples, sample_rate=48000):
    num_samples = len(samples)
    data_size = num_samples * 2
    header = struct.pack('<4sI4s', b'RIFF', 36 + data_size, b'WAVE')
    header += struct.pack('<4sIHHIIHH', b'fmt ', 16, 1, 1,
                          sample_rate, sample_rate * 2, 2, 16)
    header += struct.pack('<4sI', b'data', data_size)
    pcm = struct.pack(f'<{num_samples}h', *samples)
    return header + pcm


def call_tts(text, voice_id, lang_code, api_key):
    body = {
        "input": {"text": text},
        "voice": {"languageCode": lang_code, "name": voice_id},
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": 48000,
            "speakingRate": 1.0,
            "pitch": 0.0,
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
    pcm_data = audio_bytes[44:]
    num_samples = len(pcm_data) // 2
    samples = list(struct.unpack(f'<{num_samples}h', pcm_data))
    return samples


def generate_lang(lang, api_key):
    cfg = LANG_CONFIG[lang]
    out_dir = os.path.join(SCRIPT_DIR, cfg["folder"])
    os.makedirs(out_dir, exist_ok=True)

    idx = LANG_INDEX[lang]
    print(f"\n{'='*60}")
    print(f"Gerando: {cfg['label']} -> {out_dir}")
    print(f"{'='*60}")

    total = len(WORDS)
    ok = 0
    errors = 0

    for i, word in enumerate(WORDS, 1):
        filename = word[0]
        text = word[idx]
        wav_path = os.path.join(out_dir, f"{filename}.wav")
        print(f"  [{i:2d}/{total}] {filename}.wav -- \"{text}\"... ", end="", flush=True)

        try:
            samples = call_tts(text, cfg["voice_id"], cfg["lang_code"], api_key)
            samples = normalize_rms(samples, target_rms=11000)
            wav_data = make_wav(samples, 48000)
            with open(wav_path, "wb") as f:
                f.write(wav_data)
            size_kb = len(wav_data) / 1024
            print(f"OK ({size_kb:.1f} KB)")
            ok += 1
        except urllib.error.HTTPError as e:
            print(f"ERRO HTTP {e.code}: {e.read().decode('utf-8', errors='replace')[:100]}")
            errors += 1
        except Exception as e:
            print(f"ERRO: {e}")
            errors += 1

        time.sleep(0.3)

    print(f"\nResultado: {ok}/{total} OK, {errors} erros")
    return ok, errors


def main():
    parser = argparse.ArgumentParser(description="Gerar packs de idiomas para GESTUUM")
    parser.add_argument("--key", required=True, help="Google TTS API key")
    parser.add_argument("--lang", default="all", choices=["pt", "es", "fr", "zh", "ar", "all"])
    args = parser.parse_args()

    langs = ["pt", "es", "fr", "zh", "ar"] if args.lang == "all" else [args.lang]
    total_ok = total_err = 0

    for lang in langs:
        ok, err = generate_lang(lang, args.key)
        total_ok += ok
        total_err += err

    print(f"\n{'='*60}")
    print(f"TOTAL: {total_ok} WAVs gerados, {total_err} erros")
    print(f"{'='*60}")


if __name__ == "__main__":
    main()
