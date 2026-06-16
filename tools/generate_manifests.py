"""
GESTUUM - Generate ESP Web Tools manifests
Gera 24 manifests JSON (4 personas x 6 idiomas) + sensor_b.json em app/manifests/.

Offsets canonicos ESP32 (sensor_a/partitions.csv):
  bootloader = 0x1000    (4096)    bootloader.bin (compartilhado)
  partitions = 0x8000    (32768)   partitions.bin (compartilhado)
  app0       = 0x10000   (65536)   firmware sensor_a/b
  spiffs     = 0x290000  (2686976) WAVs + gestos JSON (so sensor_a)

IMPORTANTE (sprint F fix 2026-05-15): manifests precisam incluir bootloader
e partitions pra funcionar em hardware de fabrica (achado embedded-specialist
na auditoria). Sem isso, M5StickC Plus2 default vem com partition 4MB e o
SPIFFS em 0x290000 fica fora da regiao alocada -> silencio total.

Uso:
    python tools/generate_manifests.py
"""

import json
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
MANIFESTS_DIR = os.path.join(PROJECT_ROOT, "app", "manifests")

PERSONAS = ["homem", "mulher", "menino", "menina"]
LANGS = ["pt", "en", "es", "fr", "zh", "ar"]

LANG_LABEL = {
    "pt": "Portugues",
    "en": "English",
    "es": "Espanol",
    "fr": "Francais",
    "zh": "Mandarim",
    "ar": "Arabe",
}
PERSONA_LABEL = {
    "homem": "Homem", "mulher": "Mulher",
    "menino": "Menino", "menina": "Menina",
}

# Offsets canonicos (match partitions.csv exato)
# Sprint F v3 (2026-05-16): SPIFFS movido de 0x290000 pra 0x200000 pra liberar
# espaco (5.44MB -> 6.0MB) e acomodar voz AR/PT que estouravam particao anterior.
BOOTLOADER_OFFSET = 0x1000     # 4096
PARTITIONS_OFFSET = 0x8000     # 32768
FIRMWARE_OFFSET = 0x10000      # 65536
SPIFFS_OFFSET = 0x200000       # 2097152 (era 0x290000 ate sprint F v2)


def base_parts(firmware_path):
    """Bootloader + partitions + firmware - sempre nessa ordem."""
    return [
        {"path": "../firmware/bootloader.bin", "offset": BOOTLOADER_OFFSET},
        {"path": "../firmware/partitions.bin", "offset": PARTITIONS_OFFSET},
        {"path": firmware_path, "offset": FIRMWARE_OFFSET},
    ]


def sensor_a_manifest(persona, lang):
    parts = base_parts("../firmware/sensor_a.bin")
    parts.append({
        "path": f"../firmware/spiffs_{persona}_{lang}.bin",
        "offset": SPIFFS_OFFSET,
    })
    return {
        "name": f"GESTUUM Sensor Principal - {PERSONA_LABEL[persona]} ({LANG_LABEL[lang]})",
        "version": "1.1.0",
        "builds": [{
            "chipFamily": "ESP32",
            "parts": parts,
        }]
    }


def sensor_b_manifest():
    return {
        "name": "GESTUUM Sensor Secundario",
        "version": "1.1.0",
        "builds": [{
            "chipFamily": "ESP32",
            "parts": base_parts("../firmware/sensor_b.bin"),
        }]
    }


def main():
    os.makedirs(MANIFESTS_DIR, exist_ok=True)

    # Limpa manifests antigos (sem idioma)
    removed = 0
    for f in os.listdir(MANIFESTS_DIR):
        if f.startswith("sensor_a_") and f.endswith(".json"):
            parts = f.replace(".json", "").split("_")
            # Antigo: sensor_a_<persona>.json (3 partes)
            # Novo:   sensor_a_<persona>_<lang>.json (4 partes)
            if len(parts) == 3:
                os.remove(os.path.join(MANIFESTS_DIR, f))
                removed += 1
    print(f"  Removidos: {removed} manifests antigos sem idioma")

    # Gera 24 manifests sensor_a
    count = 0
    for persona in PERSONAS:
        for lang in LANGS:
            path = os.path.join(MANIFESTS_DIR, f"sensor_a_{persona}_{lang}.json")
            with open(path, "w", encoding="utf-8") as f:
                json.dump(sensor_a_manifest(persona, lang), f, ensure_ascii=False, indent=2)
            count += 1
    print(f"  Criados:   {count} manifests sensor_a_<persona>_<lang>.json")

    # sensor_b (1)
    path = os.path.join(MANIFESTS_DIR, "sensor_b.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(sensor_b_manifest(), f, ensure_ascii=False, indent=2)
    print(f"  Atualizado: sensor_b.json")
    print(f"\n  Total: {count + 1} manifests em {MANIFESTS_DIR}")


if __name__ == "__main__":
    main()
