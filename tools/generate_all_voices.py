"""
GESTUUM — Gerador unificado de WAVs (Google Cloud TTS).

Substitui scripts antigos:
  - generate_google_tts.py (legado, 22050Hz, sem effects profile)
  - generate_english_voices.py (atualizado, mas so en-US)
  - generate_language_packs.py (similar, fragmentado)

Specs (ver .frank-dev/stack.md > Geracao de audios):
  - LINEAR16, 48000 Hz, mono, 16-bit
  - effectsProfileId = "small-bluetooth-speaker-class-device"
  - Voz infantil (menino/menina) = mesma voz adulta + 6 semitons
  - Pos-processing: normalizacao RMS para target=11000
  - Pre-silence: 200ms

Naming (ver cristal C10):
  Mesmos 43 filenames em INGLES em todos os idiomas.

Estrutura output:
  tools/_voices_<lang>/<persona>/*.wav    (43 arquivos do vocabulario)
  app/voice_samples/<persona>_<lang>.wav  (24 samples de preview)

Uso:
  export GOOGLE_TTS_API_KEY=AIzaSy...

  # Gerar samples de preview (24 arquivos, ~20s)
  python tools/generate_all_voices.py --type preview

  # Gerar vocabulario completo de 1 idioma
  python tools/generate_all_voices.py --type vocab --lang pt

  # Gerar vocabulario de todos os idiomas faltantes
  python tools/generate_all_voices.py --type vocab --lang pt,es,fr,ar

  # Gerar 1 persona+idioma especifico
  python tools/generate_all_voices.py --type vocab --lang pt --persona homem
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

TTS_URL = "https://texttospeech.googleapis.com/v1/text:synthesize"

# Mapeamento persona → codigo de pasta (mesmo padrao usado no SPIFFS)
VOICE_DIR_MAP = {"homem": "h", "mulher": "m", "menino": "n", "menina": "i"}

# ============================================================================
# MATRIZ DE VOZES Google TTS — persona × idioma
# ============================================================================
# voice_id, gender, pitch (semitones), rate
VOICES = {
    "en": {
        "lang_code": "en-US",
        "homem":  ("en-US-Neural2-D", "MALE",   0.0, 1.0),
        "mulher": ("en-US-Neural2-F", "FEMALE", 0.0, 1.0),
        "menino": ("en-US-Neural2-D", "MALE",   6.0, 1.0),
        "menina": ("en-US-Neural2-F", "FEMALE", 6.0, 1.0),
    },
    "pt": {
        "lang_code": "pt-BR",
        "homem":  ("pt-BR-Neural2-B", "MALE",   0.0, 0.9),
        "mulher": ("pt-BR-Wavenet-A", "FEMALE", 0.0, 0.9),
        "menino": ("pt-BR-Wavenet-B", "MALE",   6.0, 0.9),
        "menina": ("pt-BR-Neural2-C", "FEMALE", 6.0, 0.9),
    },
    "es": {
        "lang_code": "es-ES",
        "homem":  ("es-ES-Neural2-B", "MALE",   0.0, 0.9),
        "mulher": ("es-ES-Neural2-A", "FEMALE", 0.0, 0.9),
        "menino": ("es-ES-Neural2-B", "MALE",   6.0, 0.9),
        "menina": ("es-ES-Neural2-A", "FEMALE", 6.0, 0.9),
    },
    "fr": {
        "lang_code": "fr-FR",
        "homem":  ("fr-FR-Neural2-B", "MALE",   0.0, 0.9),
        "mulher": ("fr-FR-Neural2-A", "FEMALE", 0.0, 0.9),
        "menino": ("fr-FR-Neural2-B", "MALE",   6.0, 0.9),
        "menina": ("fr-FR-Neural2-A", "FEMALE", 6.0, 0.9),
    },
    "zh": {
        "lang_code": "cmn-CN",
        "homem":  ("cmn-CN-Wavenet-B", "MALE",   0.0, 0.9),
        "mulher": ("cmn-CN-Wavenet-A", "FEMALE", 0.0, 0.9),
        "menino": ("cmn-CN-Wavenet-B", "MALE",   6.0, 0.9),
        "menina": ("cmn-CN-Wavenet-A", "FEMALE", 6.0, 0.9),
    },
    "ar": {
        "lang_code": "ar-XA",
        "homem":  ("ar-XA-Wavenet-B", "MALE",   0.0, 0.9),
        "mulher": ("ar-XA-Wavenet-A", "FEMALE", 0.0, 0.9),
        "menino": ("ar-XA-Wavenet-B", "MALE",   6.0, 0.9),
        "menina": ("ar-XA-Wavenet-A", "FEMALE", 6.0, 0.9),
    },
}

# ============================================================================
# FRASES DE PREVIEW (saudacao por idioma)
# ============================================================================
PREVIEW_TEXTS = {
    "en": "Hello, I am GESTUUM. Your gestures, your voice.",
    "pt": "Ola, eu sou o GESTUUM. Seus gestos, sua voz.",
    "es": "Hola, soy GESTUUM. Tus gestos, tu voz.",
    "fr": "Bonjour, je suis GESTUUM. Vos gestes, votre voix.",
    "zh": "你好，我是 GESTUUM。你的手势，你的声音。",
    "ar": "مرحبًا، أنا GESTUUM. إيماءاتك، صوتك.",
}

# ============================================================================
# VOCABULARIO — 43 filenames (em ingles, identicos em todos idiomas)
# ============================================================================
# Cada item: (filename_sem_extensao, dict de textos por idioma)
# Filenames seguem padrao em INGLES (cristal C10).

CONTEXTS = [
    ("ctx_01_i_want", {
        "en": "I want", "pt": "Eu quero", "es": "Yo quiero",
        "fr": "Je veux", "zh": "我要", "ar": "أنا أريد",
    }),
    ("ctx_02_i_dont_want", {
        "en": "I don't want", "pt": "Eu nao quero", "es": "Yo no quiero",
        "fr": "Je ne veux pas", "zh": "我不要", "ar": "أنا لا أريد",
    }),
    ("ctx_03_i_need", {
        "en": "I need", "pt": "Eu preciso", "es": "Yo necesito",
        "fr": "J'ai besoin", "zh": "我需要", "ar": "أنا أحتاج",
    }),
    ("ctx_04_i_feel", {
        "en": "I feel", "pt": "Eu sinto", "es": "Yo siento",
        "fr": "Je me sens", "zh": "我觉得", "ar": "أنا أشعر",
    }),
    ("ctx_05_where_is", {
        "en": "Where is", "pt": "Onde esta", "es": "Donde esta",
        "fr": "Ou est", "zh": "在哪里", "ar": "أين",
    }),
    ("ctx_06_call", {
        "en": "Call", "pt": "Chame", "es": "Llama",
        "fr": "Appelle", "zh": "请叫", "ar": "اتصل",
    }),
    ("ctx_07_i_am", {
        "en": "I am", "pt": "Eu estou", "es": "Yo estoy",
        "fr": "Je suis", "zh": "我是", "ar": "أنا",
    }),
    ("ctx_08_i_am_not", {
        "en": "I am not", "pt": "Eu nao estou", "es": "Yo no estoy",
        "fr": "Je ne suis pas", "zh": "我不是", "ar": "أنا لست",
    }),
]

OBJECTS = [
    ("water",        {"en": "water",        "pt": "agua",          "es": "agua",          "fr": "eau",            "zh": "水",       "ar": "ماء"}),
    ("food",         {"en": "food",         "pt": "comida",        "es": "comida",        "fr": "nourriture",     "zh": "食物",     "ar": "طعام"}),
    ("bathroom",     {"en": "bathroom",     "pt": "banheiro",      "es": "bano",          "fr": "toilettes",      "zh": "卫生间",   "ar": "حمام"}),
    ("medicine",     {"en": "medicine",     "pt": "remedio",       "es": "medicina",      "fr": "medicament",     "zh": "药",       "ar": "دواء"}),
    ("rest",         {"en": "rest",         "pt": "descanso",      "es": "descanso",      "fr": "repos",          "zh": "休息",     "ar": "راحة"}),
    ("pain",         {"en": "pain",         "pt": "dor",           "es": "dolor",         "fr": "douleur",        "zh": "疼痛",     "ar": "ألم"}),
    ("head",         {"en": "head",         "pt": "cabeca",        "es": "cabeza",        "fr": "tete",           "zh": "头",       "ar": "رأس"}),
    ("chest",        {"en": "chest",        "pt": "peito",         "es": "pecho",         "fr": "poitrine",       "zh": "胸",       "ar": "صدر"}),
    ("hungry",       {"en": "hungry",       "pt": "fome",          "es": "hambre",        "fr": "faim",           "zh": "饿",       "ar": "جائع"}),
    ("thirsty",      {"en": "thirsty",      "pt": "sede",          "es": "sed",           "fr": "soif",           "zh": "渴",       "ar": "عطشان"}),
    ("room",         {"en": "room",         "pt": "quarto",        "es": "habitacion",    "fr": "chambre",        "zh": "房间",     "ar": "غرفة"}),
    ("living_room",  {"en": "living room",  "pt": "sala de estar", "es": "sala de estar", "fr": "salon",          "zh": "客厅",     "ar": "غرفة المعيشة"}),
    ("light",        {"en": "light",        "pt": "luz",           "es": "luz",           "fr": "lumiere",        "zh": "灯",       "ar": "ضوء"}),
    ("door",         {"en": "door",         "pt": "porta",         "es": "puerta",        "fr": "porte",          "zh": "门",       "ar": "باب"}),
    ("window",       {"en": "window",       "pt": "janela",        "es": "ventana",       "fr": "fenetre",        "zh": "窗",       "ar": "نافذة"}),
    ("doctor",       {"en": "doctor",       "pt": "medico",        "es": "medico",        "fr": "medecin",        "zh": "医生",     "ar": "طبيب"}),
    ("nurse",        {"en": "nurse",        "pt": "enfermeiro",    "es": "enfermero",     "fr": "infirmier",      "zh": "护士",     "ar": "ممرض"}),
    ("teacher",      {"en": "teacher",      "pt": "professor",     "es": "profesor",      "fr": "professeur",     "zh": "老师",     "ar": "معلم"}),
    ("sleep",        {"en": "sleep",        "pt": "dormir",        "es": "dormir",        "fr": "dormir",         "zh": "睡觉",     "ar": "نوم"}),
    ("eat",          {"en": "eat",          "pt": "comer",         "es": "comer",         "fr": "manger",         "zh": "吃",       "ar": "أكل"}),
    ("ambulance",    {"en": "ambulance",    "pt": "ambulancia",    "es": "ambulancia",    "fr": "ambulance",      "zh": "救护车",   "ar": "إسعاف"}),
    ("cant_breathe", {"en": "can't breathe","pt": "nao consigo respirar", "es": "no puedo respirar", "fr": "je ne peux pas respirer", "zh": "我不能呼吸", "ar": "لا أستطيع التنفس"}),
    ("class",        {"en": "class",        "pt": "aula",          "es": "clase",         "fr": "classe",         "zh": "课",       "ar": "صف"}),
    ("break_time",   {"en": "break time",   "pt": "intervalo",     "es": "recreo",        "fr": "recreation",     "zh": "休息时间", "ar": "وقت الاستراحة"}),
    ("snack",        {"en": "snack",        "pt": "lanche",        "es": "merienda",      "fr": "gouter",         "zh": "点心",     "ar": "وجبة خفيفة"}),
]

SOLOS = [
    ("help",          {"en": "Help!",        "pt": "Socorro!",     "es": "Ayuda!",       "fr": "Au secours!",  "zh": "救命！",   "ar": "النجدة!"}),
    ("yes",           {"en": "Yes",          "pt": "Sim",          "es": "Si",           "fr": "Oui",          "zh": "是",       "ar": "نعم"}),
    ("no",            {"en": "No",           "pt": "Nao",          "es": "No",           "fr": "Non",          "zh": "不",       "ar": "لا"}),
    ("hi",            {"en": "Hi!",          "pt": "Oi!",          "es": "Hola!",        "fr": "Salut!",       "zh": "你好！",   "ar": "مرحبا!"}),
    ("bye",           {"en": "Bye!",         "pt": "Tchau!",       "es": "Adios!",       "fr": "Au revoir!",   "zh": "再见！",   "ar": "وداعا!"}),
    ("thank_you",     {"en": "Thank you",    "pt": "Obrigado",     "es": "Gracias",      "fr": "Merci",        "zh": "谢谢",     "ar": "شكرا"}),
    ("please",        {"en": "Please",       "pt": "Por favor",    "es": "Por favor",    "fr": "S'il vous plait", "zh": "请",     "ar": "من فضلك"}),
    ("good_morning",  {"en": "Good morning", "pt": "Bom dia",      "es": "Buenos dias",  "fr": "Bonjour",      "zh": "早上好",   "ar": "صباح الخير"}),
    ("ok",            {"en": "OK",           "pt": "OK",           "es": "Vale",         "fr": "OK",           "zh": "好的",     "ar": "حسنا"}),
    ("i_need_help",   {"en": "I need help!", "pt": "Preciso de ajuda!", "es": "Necesito ayuda!", "fr": "J'ai besoin d'aide!", "zh": "我需要帮助！", "ar": "أحتاج مساعدة!"}),
]

# ============================================================================
# Perfis legacy (Hospital + Trabalho + Transporte) — 35 itens extras
# Texto PT vem de docs/GESTUUM_Vocabulario_v2.md. Outros idiomas: traduzido.
# ============================================================================

HOSPITAL = [
    ("enfermeira",     {"en": "duty nurse",          "pt": "enfermeira",            "es": "enfermera",            "fr": "infirmiere de garde",  "zh": "值班护士",     "ar": "ممرضة المناوبة"}),
    ("medico_plantao", {"en": "duty doctor",         "pt": "medico de plantao",     "es": "medico de guardia",    "fr": "medecin de garde",     "zh": "值班医生",     "ar": "طبيب المناوبة"}),
    ("soro",           {"en": "IV drip",             "pt": "soro",                  "es": "suero",                "fr": "perfusion",            "zh": "点滴",         "ar": "محلول وريدي"}),
    ("exame",          {"en": "exam",                "pt": "exame",                 "es": "examen",               "fr": "examen",               "zh": "检查",         "ar": "فحص"}),
    ("visita",         {"en": "visit",               "pt": "visita",                "es": "visita",               "fr": "visite",               "zh": "探视",         "ar": "زيارة"}),
    ("alta",           {"en": "discharge",           "pt": "alta",                  "es": "alta",                 "fr": "sortie",               "zh": "出院",         "ar": "خروج"}),
    ("posicao_cama",   {"en": "change bed position", "pt": "mudar posicao da cama", "es": "cambiar posicion de la cama", "fr": "changer la position du lit", "zh": "调整床的位置", "ar": "تغيير وضعية السرير"}),
    ("cobertor",       {"en": "blanket",             "pt": "cobertor",              "es": "manta",                "fr": "couverture",           "zh": "毛毯",         "ar": "بطانية"}),
    ("leito",          {"en": "bed",                 "pt": "leito",                 "es": "cama",                 "fr": "lit",                  "zh": "病床",         "ar": "سرير"}),
    ("curativo",       {"en": "bandage",             "pt": "curativo",              "es": "vendaje",              "fr": "pansement",            "zh": "敷料",         "ar": "ضمادة"}),
    ("alergia_hosp",   {"en": "allergy",             "pt": "alergia",               "es": "alergia",              "fr": "allergie",             "zh": "过敏",         "ar": "حساسية"}),
    ("nausea",         {"en": "nausea",              "pt": "nausea",                "es": "nausea",               "fr": "nausee",               "zh": "恶心",         "ar": "غثيان"}),
    ("dieta",          {"en": "diet",                "pt": "dieta",                 "es": "dieta",                "fr": "regime",               "zh": "饮食",         "ar": "حمية"}),
]

TRABALHO = [
    ("reuniao",     {"en": "meeting",            "pt": "reuniao",            "es": "reunion",          "fr": "reunion",          "zh": "会议",       "ar": "اجتماع"}),
    ("escritorio",  {"en": "office",             "pt": "escritorio",         "es": "oficina",          "fr": "bureau",           "zh": "办公室",     "ar": "مكتب"}),
    ("computador",  {"en": "computer",           "pt": "computador",         "es": "computadora",      "fr": "ordinateur",       "zh": "电脑",       "ar": "حاسوب"}),
    ("documento",   {"en": "document",           "pt": "documento",          "es": "documento",        "fr": "document",         "zh": "文件",       "ar": "وثيقة"}),
    ("email",       {"en": "email",              "pt": "e-mail",             "es": "correo",           "fr": "courriel",         "zh": "电邮",       "ar": "بريد إلكتروني"}),
    ("cafe",        {"en": "coffee",             "pt": "cafe",               "es": "cafe",             "fr": "cafe",             "zh": "咖啡",       "ar": "قهوة"}),
    ("pausa",       {"en": "break",              "pt": "pausa",              "es": "pausa",            "fr": "pause",            "zh": "休息",       "ar": "استراحة"}),
    ("comecar",     {"en": "start",              "pt": "comecar",            "es": "empezar",          "fr": "commencer",        "zh": "开始",       "ar": "ابدأ"}),
    ("terminar",    {"en": "finish",             "pt": "terminar",           "es": "terminar",         "fr": "terminer",         "zh": "结束",       "ar": "إنهاء"}),
    ("ajuda_trab",  {"en": "I need help",        "pt": "preciso de ajuda",   "es": "necesito ayuda",   "fr": "j'ai besoin d'aide", "zh": "我需要帮助", "ar": "أحتاج مساعدة"}),
]

TRANSPORTE = [
    ("motorista",  {"en": "driver",      "pt": "motorista",  "es": "conductor", "fr": "chauffeur",  "zh": "司机",   "ar": "سائق"}),
    ("parada",     {"en": "stop",        "pt": "parada",     "es": "parada",    "fr": "arret",      "zh": "车站",   "ar": "محطة"}),
    ("destino",    {"en": "destination", "pt": "destino",    "es": "destino",   "fr": "destination","zh": "目的地", "ar": "وجهة"}),
    ("endereco",   {"en": "address",     "pt": "endereco",   "es": "direccion", "fr": "adresse",    "zh": "地址",   "ar": "عنوان"}),
    ("mapa",       {"en": "map",         "pt": "mapa",       "es": "mapa",      "fr": "carte",      "zh": "地图",   "ar": "خريطة"}),
    ("chegou",     {"en": "arrived",     "pt": "chegou",     "es": "llego",     "fr": "arrive",     "zh": "到达",   "ar": "وصل"}),
    ("esperar",    {"en": "wait",        "pt": "esperar",    "es": "esperar",   "fr": "attendre",   "zh": "等待",   "ar": "انتظر"}),
    ("bilhete",    {"en": "ticket",      "pt": "bilhete",    "es": "billete",   "fr": "billet",     "zh": "车票",   "ar": "تذكرة"}),
    ("aeroporto",  {"en": "airport",     "pt": "aeroporto",  "es": "aeropuerto","fr": "aeroport",   "zh": "机场",   "ar": "مطار"}),
    ("hotel",      {"en": "hotel",       "pt": "hotel",      "es": "hotel",     "fr": "hotel",      "zh": "酒店",   "ar": "فندق"}),
    ("taxi",       {"en": "taxi",        "pt": "taxi",       "es": "taxi",      "fr": "taxi",       "zh": "出租车", "ar": "سيارة أجرة"}),
    ("onibus",     {"en": "bus",         "pt": "onibus",     "es": "autobus",   "fr": "autobus",    "zh": "公交车", "ar": "حافلة"}),
]

# Override de palavras para vozes femininas (genero gramatical)
FEMALE_OVERRIDES = {
    "pt": {"thank_you": "Obrigada"},
    "es": {},   # gracias e neutro
    "fr": {},   # merci e neutro
    "ar": {"hungry": "جائعة", "thirsty": "عطشى"},
    "en": {},
    "zh": {},
}

ALL_VOCAB = CONTEXTS + OBJECTS + SOLOS + HOSPITAL + TRABALHO + TRANSPORTE  # 8 + 25 + 10 + 13 + 10 + 12 = 78


# ============================================================================
# Audio processing
# ============================================================================

def normalize_rms(samples, target_rms=11000):
    """Normaliza amplitude pelo RMS para volume perceptivo consistente."""
    if not samples:
        return samples
    sum_sq = sum(s * s for s in samples)
    rms = (sum_sq / len(samples)) ** 0.5
    if rms < 1.0:
        return samples
    gain = target_rms / rms
    result = []
    for s in samples:
        v = int(s * gain)
        if v > 32767:
            v = 32767
        elif v < -32768:
            v = -32768
        result.append(v)
    return result


def add_pre_silence(samples, ms=200, sample_rate=48000):
    """Adiciona N ms de silencio no inicio."""
    silence_samples = int(sample_rate * ms / 1000)
    return [0] * silence_samples + samples


def make_wav(samples, sample_rate=48000):
    """Cria WAV 16-bit mono PCM a partir de lista de samples int16."""
    n = len(samples)
    data_size = n * 2
    header = struct.pack('<4sI4s', b'RIFF', 36 + data_size, b'WAVE')
    header += struct.pack('<4sIHHIIHH', b'fmt ', 16, 1, 1,
                          sample_rate, sample_rate * 2, 2, 16)
    header += struct.pack('<4sI', b'data', data_size)
    pcm = struct.pack(f'<{n}h', *samples)
    return header + pcm


def call_tts(text, lang_code, voice_id, gender, pitch, rate, api_key, retries=3):
    """Chama Google Cloud TTS e retorna lista de samples int16 (48kHz mono)."""
    body = {
        "input": {"text": text},
        "voice": {
            "languageCode": lang_code,
            "name": voice_id,
            "ssmlGender": gender,
        },
        "audioConfig": {
            "audioEncoding": "LINEAR16",
            "sampleRateHertz": 48000,
            "speakingRate": rate,
            "pitch": pitch,
            "effectsProfileId": ["small-bluetooth-speaker-class-device"],
        },
    }
    data = json.dumps(body).encode("utf-8")

    last_err = None
    for attempt in range(retries):
        try:
            req = urllib.request.Request(
                f"{TTS_URL}?key={api_key}",
                data=data,
                headers={"Content-Type": "application/json"},
            )
            with urllib.request.urlopen(req, timeout=30) as resp:
                result = json.loads(resp.read().decode("utf-8"))
            audio_bytes = base64.b64decode(result["audioContent"])
            pcm_data = audio_bytes[44:]   # pula header WAV do Google
            n = len(pcm_data) // 2
            return list(struct.unpack(f'<{n}h', pcm_data))
        except urllib.error.HTTPError as e:
            last_err = e
            err_body = e.read().decode("utf-8", errors="replace")[:200]
            print(f"      [retry {attempt+1}/{retries}] HTTP {e.code}: {err_body}")
            time.sleep(2 * (attempt + 1))
        except Exception as e:
            last_err = e
            print(f"      [retry {attempt+1}/{retries}] {type(e).__name__}: {e}")
            time.sleep(2 * (attempt + 1))

    raise last_err if last_err else RuntimeError("TTS failed (no error captured)")


# ============================================================================
# Pipelines
# ============================================================================

def generate_one_wav(text, lang, persona, api_key, out_path, sample_rate=48000):
    """Gera 1 WAV: chama TTS, normaliza, add silencio, salva."""
    voice_cfg = VOICES[lang][persona]
    voice_id, gender, pitch, rate = voice_cfg
    lang_code = VOICES[lang]["lang_code"]

    samples = call_tts(text, lang_code, voice_id, gender, pitch, rate, api_key)
    samples = normalize_rms(samples, target_rms=11000)
    samples = add_pre_silence(samples, ms=200, sample_rate=sample_rate)
    wav = make_wav(samples, sample_rate=sample_rate)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(wav)
    return len(wav)


def generate_previews(api_key, langs, personas, skip_existing=True):
    """Gera samples de preview em app/voice_samples/<persona>_<lang>.wav."""
    out_dir = os.path.join(PROJECT_ROOT, "app", "voice_samples")
    os.makedirs(out_dir, exist_ok=True)

    total_planned = len(personas) * len(langs)
    print(f"\n{'='*60}")
    print(f"  PREVIEW SAMPLES — {len(personas)} personas × {len(langs)} idiomas = {total_planned}")
    print(f"  Output: {out_dir}")
    print(f"  Skip existing: {skip_existing}")
    print(f"{'='*60}")

    ok, errors, skipped = 0, 0, 0
    for lang in langs:
        text = PREVIEW_TEXTS[lang]
        for persona in personas:
            filename = f"{persona}_{lang}.wav"
            out_path = os.path.join(out_dir, filename)
            if skip_existing and os.path.exists(out_path):
                print(f"  [{lang}/{persona}] {filename} ... SKIP (ja existe)")
                skipped += 1
                continue
            print(f"  [{lang}/{persona}] {filename} ... ", end="", flush=True)
            try:
                size = generate_one_wav(text, lang, persona, api_key, out_path)
                print(f"OK ({size//1024} KB)")
                ok += 1
            except Exception as e:
                print(f"ERRO: {e}")
                errors += 1
            time.sleep(0.3)

    print(f"\n  Resultado: {ok} OK, {errors} erros, {skipped} skipped")
    return ok, errors


def generate_vocab(api_key, lang, persona, skip_existing=True):
    """Gera os 78 WAVs do vocabulario para 1 persona × 1 idioma."""
    persona_code = VOICE_DIR_MAP[persona]
    out_dir = os.path.join(PROJECT_ROOT, "tools", f"_voices_{lang}", persona_code)
    os.makedirs(out_dir, exist_ok=True)

    overrides = FEMALE_OVERRIDES.get(lang, {})
    is_female = persona in ("mulher", "menina")

    print(f"\n{'='*60}")
    print(f"  VOCABULARIO — {lang} / {persona}")
    print(f"  Voz: {VOICES[lang][persona][0]} (pitch {VOICES[lang][persona][2]:+.0f}st)")
    print(f"  Output: {out_dir}")
    print(f"  Skip existing: {skip_existing}")
    print(f"{'='*60}")

    ok, errors, skipped = 0, 0, 0
    total = len(ALL_VOCAB)
    for i, (filename, texts) in enumerate(ALL_VOCAB, 1):
        text = texts[lang]
        if is_female and filename in overrides:
            text = overrides[filename]
        out_path = os.path.join(out_dir, f"{filename}.wav")
        if skip_existing and os.path.exists(out_path):
            print(f"  [{i:2d}/{total}] {filename}.wav SKIP (ja existe)")
            skipped += 1
            continue
        print(f"  [{i:2d}/{total}] {filename}.wav '{text}' ... ", end="", flush=True)
        try:
            size = generate_one_wav(text, lang, persona, api_key, out_path)
            print(f"OK ({size//1024} KB)")
            ok += 1
        except Exception as e:
            print(f"ERRO: {e}")
            errors += 1
        time.sleep(0.3)

    print(f"\n  {lang}/{persona}: {ok}/{total-skipped} OK ({errors} erros, {skipped} skipped)")
    return ok, errors


# ============================================================================
# Main
# ============================================================================

def main():
    parser = argparse.ArgumentParser(description="GESTUUM — Gerador unificado de WAVs (Google TTS)")
    parser.add_argument("--key", help="Google Cloud API key (ou env GOOGLE_TTS_API_KEY)")
    parser.add_argument("--type", choices=["preview", "vocab", "all"], default="preview",
                        help="O que gerar (default: preview)")
    parser.add_argument("--lang", default="all",
                        help="Idiomas: en,pt,es,fr,zh,ar ou 'all' (default: all)")
    parser.add_argument("--persona", default="all",
                        help="Personas: homem,mulher,menino,menina ou 'all' (default: all)")
    parser.add_argument("--no-skip-existing", action="store_true",
                        help="Regenera arquivos existentes (default: skip)")
    args = parser.parse_args()
    skip_existing = not args.no_skip_existing

    api_key = args.key or os.environ.get("GOOGLE_TTS_API_KEY")
    if not api_key:
        print("ERRO: API key necessaria.")
        print("  export GOOGLE_TTS_API_KEY=AIzaSy...")
        print("  ou: python tools/generate_all_voices.py --key AIzaSy...")
        sys.exit(1)

    langs = list(VOICES.keys()) if args.lang == "all" else args.lang.split(",")
    personas = list(VOICE_DIR_MAP.keys()) if args.persona == "all" else args.persona.split(",")

    # Validacao
    for lg in langs:
        if lg not in VOICES:
            print(f"ERRO: idioma invalido: {lg} (validos: {list(VOICES.keys())})")
            sys.exit(1)
    for pe in personas:
        if pe not in VOICE_DIR_MAP:
            print(f"ERRO: persona invalida: {pe} (validas: {list(VOICE_DIR_MAP.keys())})")
            sys.exit(1)

    grand_ok, grand_err = 0, 0
    t0 = time.time()

    if args.type in ("preview", "all"):
        ok, err = generate_previews(api_key, langs, personas, skip_existing=skip_existing)
        grand_ok += ok
        grand_err += err

    if args.type in ("vocab", "all"):
        for lg in langs:
            for pe in personas:
                ok, err = generate_vocab(api_key, lg, pe, skip_existing=skip_existing)
                grand_ok += ok
                grand_err += err

    elapsed = time.time() - t0
    print(f"\n{'='*60}")
    print(f"  FINAL: {grand_ok} OK, {grand_err} erros em {elapsed:.0f}s")
    print(f"{'='*60}")

    sys.exit(1 if grand_err > 0 else 0)


if __name__ == "__main__":
    main()
