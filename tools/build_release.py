"""
GESTUUM - Build Release Binaries
Compila firmwares e gera 24 SPIFFS (4 personas x 6 idiomas) para o PWA web installer.

Uso:
    python tools/build_release.py [--skip-firmware] [--only-lang=pt,en,...]

Output:
    app/firmware/
    +- sensor_a.bin                  # Firmware Sensor A
    +- sensor_b.bin                  # Firmware Sensor B
    +- spiffs_<persona>_<lang>.bin   # 24 SPIFFS images

Decisoes arquiteturais:
  - SPIFFS comporta 1 persona x 1 idioma (~3.5MB de 5.44MB).
  - WAVs vao SEMPRE em data/a/h/ (caminho default do firmware). Trocar
    persona/idioma exige re-flash (cristal C04).
  - Vocabulario core 43 WAVs.

Pre-requisitos:
  - PlatformIO CLI
  - WAVs em tools/_voices_<lang>/<persona>/
"""

import argparse
import os
import shutil
import subprocess
import sys
import time

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SENSOR_A_DIR = os.path.join(PROJECT_ROOT, "sensor_a")
SENSOR_B_DIR = os.path.join(PROJECT_ROOT, "sensor_b")
DATA_DIR = os.path.join(SENSOR_A_DIR, "data")
VOICE_DIR = os.path.join(DATA_DIR, "a", "h")  # caminho fixo onde firmware le
FIRMWARE_OUT = os.path.join(PROJECT_ROOT, "app", "firmware")

PERSONAS = ["homem", "mulher", "menino", "menina"]
PERSONA_CODE = {"homem": "h", "mulher": "m", "menino": "n", "menina": "i"}
LANGS = ["pt", "en", "es", "fr", "zh", "ar"]

CORE_VOCAB = [
    "ctx_01_i_want", "ctx_02_i_dont_want", "ctx_03_i_need", "ctx_04_i_feel",
    "ctx_05_where_is", "ctx_06_call", "ctx_07_i_am", "ctx_08_i_am_not",
    "water", "food", "bathroom", "medicine", "rest", "pain", "head", "chest",
    "hungry", "thirsty", "room", "living_room", "light", "door", "window",
    "doctor", "nurse", "teacher", "sleep", "eat", "ambulance", "cant_breathe",
    "class", "break_time", "snack",
    "help", "yes", "no", "hi", "bye", "thank_you", "please", "good_morning",
    "ok", "i_need_help",
]


def log(msg):
    print(msg, flush=True)


def remove_file_retry(path, retries=5, delay=0.4):
    """Remove arquivo com retry (Windows pode ter handle aberto)."""
    for i in range(retries):
        try:
            if os.path.isfile(path):
                os.remove(path)
            return True
        except (PermissionError, OSError):
            if i == retries - 1:
                raise
            time.sleep(delay)
    return False


def clean_voice_dir():
    """Remove TODOS arquivos .wav dentro de data/a/h/ (sem mexer no diretorio)."""
    os.makedirs(VOICE_DIR, exist_ok=True)
    for f in os.listdir(VOICE_DIR):
        if f.endswith(".wav"):
            remove_file_retry(os.path.join(VOICE_DIR, f))


def populate_voice_dir(persona, lang):
    """Copia 43 WAVs core de tools/_voices_<lang>/<persona>/ pra data/a/h/."""
    voice_src = os.path.join(SCRIPT_DIR, f"_voices_{lang}", PERSONA_CODE[persona])
    if not os.path.isdir(voice_src):
        return False, f"pasta nao existe: {voice_src}"

    clean_voice_dir()
    missing = []
    copied = 0
    for word in CORE_VOCAB:
        src = os.path.join(voice_src, f"{word}.wav")
        if not os.path.isfile(src):
            missing.append(word)
            continue
        shutil.copy2(src, os.path.join(VOICE_DIR, f"{word}.wav"))
        copied += 1
    if missing:
        return False, f"WAVs faltando ({len(missing)}/{len(CORE_VOCAB)}): {missing[:5]}"
    return True, f"{copied} WAVs"


def snapshot_voice_dir():
    """Backup runtime apenas dos .wav de data/a/h/ (lista de paths copiados)."""
    snapshot = []
    if os.path.isdir(VOICE_DIR):
        for f in os.listdir(VOICE_DIR):
            if f.endswith(".wav"):
                src = os.path.join(VOICE_DIR, f)
                dst_tmp = os.path.join(SCRIPT_DIR, "_voicebackup", f)
                os.makedirs(os.path.dirname(dst_tmp), exist_ok=True)
                shutil.copy2(src, dst_tmp)
                snapshot.append((dst_tmp, f))
    log(f"  [snapshot] {len(snapshot)} WAVs preservados em tools/_voicebackup/")
    return snapshot


def restore_voice_dir(snapshot):
    """Restaura .wav de data/a/h/ a partir do snapshot."""
    try:
        clean_voice_dir()
        for src_tmp, fname in snapshot:
            shutil.copy2(src_tmp, os.path.join(VOICE_DIR, fname))
        log(f"  [restore] {len(snapshot)} WAVs restaurados em data/a/h/")
    finally:
        backup_dir = os.path.join(SCRIPT_DIR, "_voicebackup")
        if os.path.isdir(backup_dir):
            try:
                for f in os.listdir(backup_dir):
                    remove_file_retry(os.path.join(backup_dir, f))
                os.rmdir(backup_dir)
            except OSError:
                pass


def run_cmd(cmd, cwd=None):
    log(f"    $ {' '.join(cmd)}")
    result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    if result.returncode != 0:
        return False, result.stderr[-400:]
    return True, ""


def build_firmware(name, project_dir):
    log(f"\n  [{name}] Compilando firmware...")
    ok, err = run_cmd(["pio", "run"], cwd=project_dir)
    if not ok:
        log(f"    ERRO: {err}")
        return False
    bin_path = os.path.join(project_dir, ".pio", "build", name, "firmware.bin")
    if not os.path.isfile(bin_path):
        log(f"    ERRO: firmware.bin nao encontrado")
        return False
    dest = os.path.join(FIRMWARE_OUT, f"{name}.bin")
    shutil.copy2(bin_path, dest)
    log(f"    OK: {dest} ({os.path.getsize(dest) // 1024} KB)")
    return True


def build_spiffs(persona, lang):
    label = f"spiffs_{persona}_{lang}"
    log(f"\n  [{label}]")
    ok, msg = populate_voice_dir(persona, lang)
    if not ok:
        log(f"    ERRO populate: {msg}")
        return False
    log(f"    populate: {msg}")

    # Fix 2026-05-16: mkspiffs trunca arquivos com timestamps muito recentes
    # quando rodado logo apos populate_voice_dir. Forcar:
    # 1. Sleep curto pra timestamps assentarem
    # 2. Apagar spiffs.bin antigo pra forcar regeneracao
    # 3. Validar contagem de arquivos no resultado
    time.sleep(0.6)
    stale_spiffs = os.path.join(SENSOR_A_DIR, ".pio", "build", "sensor_a", "spiffs.bin")
    if os.path.isfile(stale_spiffs):
        try:
            os.remove(stale_spiffs)
        except OSError:
            pass

    ok, err = run_cmd(["pio", "run", "-t", "buildfs", "-e", "sensor_a"], cwd=SENSOR_A_DIR)
    if not ok:
        log(f"    ERRO buildfs: {err}")
        return False
    spiffs_src = stale_spiffs
    if not os.path.isfile(spiffs_src):
        log(f"    ERRO: spiffs.bin nao gerado")
        return False

    # Validacao: SPIFFS deve conter os 43 WAVs + 7 JSONs = 50 arquivos.
    # Se truncou (bug do mkspiffs com timestamps), abortar.
    expected_files = len(CORE_VOCAB) + 7  # 43 WAVs + 7 JSONs em /g/
    n_voice = len(os.listdir(VOICE_DIR))
    n_g = len(os.listdir(os.path.join(DATA_DIR, "g")))
    log(f"    fonte: {n_voice} WAVs + {n_g} JSONs em data/")
    if n_voice != len(CORE_VOCAB) or n_g != 7:
        log(f"    AVISO fonte: esperado {len(CORE_VOCAB)} WAVs + 7 JSONs")

    dest = os.path.join(FIRMWARE_OUT, f"{label}.bin")
    shutil.copy2(spiffs_src, dest)
    log(f"    OK: {dest} ({os.path.getsize(dest) // 1024} KB)")
    return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--skip-firmware", action="store_true")
    parser.add_argument("--skip-spiffs", action="store_true")
    parser.add_argument("--only-lang", default="")
    parser.add_argument("--only-persona", default="")
    args = parser.parse_args()

    langs = args.only_lang.split(",") if args.only_lang else LANGS
    personas = args.only_persona.split(",") if args.only_persona else PERSONAS

    log("=" * 60)
    log("  GESTUUM - Build Release Binaries (Sprint F)")
    log(f"  Personas: {personas}")
    log(f"  Idiomas:  {langs}")
    log("=" * 60)

    if not shutil.which("pio"):
        log("ERRO: PlatformIO nao encontrado")
        sys.exit(1)

    os.makedirs(FIRMWARE_OUT, exist_ok=True)
    results = {}

    if not args.skip_firmware:
        log("\n[ETAPA 1] Firmwares")
        results["sensor_a"] = build_firmware("sensor_a", SENSOR_A_DIR)
        results["sensor_b"] = build_firmware("sensor_b", SENSOR_B_DIR)
    else:
        log("\n[ETAPA 1] Firmwares: SKIP")

    if not args.skip_spiffs:
        n = len(personas) * len(langs)
        log(f"\n[ETAPA 2] SPIFFS ({n} imagens)")
        snapshot = snapshot_voice_dir()
        try:
            i = 0
            for persona in personas:
                for lang in langs:
                    i += 1
                    log(f"\n--- [{i}/{n}] ---")
                    label = f"spiffs_{persona}_{lang}"
                    results[label] = build_spiffs(persona, lang)
        finally:
            restore_voice_dir(snapshot)
    else:
        log("\n[ETAPA 2] SPIFFS: SKIP")

    log("\n" + "=" * 60)
    log("  RESULTADO")
    log("=" * 60)
    all_ok = True
    for name, ok in results.items():
        status = "OK" if ok else "FALHOU"
        if not ok:
            all_ok = False
        log(f"  {name:30s} {status}")
    log("")
    sys.exit(0 if all_ok else 1)


if __name__ == "__main__":
    main()
