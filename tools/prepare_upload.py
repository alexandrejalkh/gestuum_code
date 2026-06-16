"""
GESTUUM — Prepare SPIFFS Upload
Prepara a pasta data/ do sensor_a com apenas 1 voz selecionada
para upload via PlatformIO (pio run -t uploadfs).

Usage:
    python prepare_upload.py                  → menu interativo
    python prepare_upload.py --voice homem    → direto, sem menu
    python prepare_upload.py --voice menina --upload  → prepara e faz upload

Requirements:
    - PlatformIO CLI (pio) instalado (só se usar --upload)
"""

import argparse
import os
import shutil
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SENSOR_A_DIR = os.path.join(PROJECT_ROOT, "sensor_a")
# FIX: Paths encurtados para caber no SPIFFS (max 31 chars)
# audio/ → a/, gestures/ → g/, homem→h, mulher→m, menino→n, menina→i
DATA_DIR = os.path.join(SENSOR_A_DIR, "data")
AUDIO_DIR = os.path.join(DATA_DIR, "a")
AUDIO_SOURCE_DIR = os.path.join(SENSOR_A_DIR, "data", "a")
GESTURES_DIR = os.path.join(DATA_DIR, "g")

# Vozes disponiveis — dir e o codigo curto da pasta no SPIFFS
VOICES = {
    "homem":  {"label": "Homem (voz masculina adulta)",    "dir": "h"},
    "mulher": {"label": "Mulher (voz feminina adulta)",    "dir": "m"},
    "menino": {"label": "Menino (voz masculina infantil)", "dir": "n"},
    "menina": {"label": "Menina (voz feminina infantil)",  "dir": "i"},
}

# Diretório de backup para as vozes que não vão no upload
VOICES_BACKUP_DIR = os.path.join(PROJECT_ROOT, "tools", "_voices_backup")


def count_files(directory, ext=".wav"):
    """Count files with given extension in directory."""
    if not os.path.isdir(directory):
        return 0
    return len([f for f in os.listdir(directory) if f.endswith(ext)])


def get_dir_size_mb(directory):
    """Get directory size in MB."""
    total = 0
    if not os.path.isdir(directory):
        return 0
    for dirpath, dirnames, filenames in os.walk(directory):
        for f in filenames:
            fp = os.path.join(dirpath, f)
            total += os.path.getsize(fp)
    return total / 1024 / 1024


def backup_voices():
    """Move all voice folders to backup, preserving them."""
    os.makedirs(VOICES_BACKUP_DIR, exist_ok=True)
    for voice_key, voice_cfg in VOICES.items():
        src = os.path.join(AUDIO_DIR, voice_cfg["dir"])
        dst = os.path.join(VOICES_BACKUP_DIR, voice_cfg["dir"])
        if os.path.isdir(src) and not os.path.isdir(dst):
            print(f"  Backup: {voice_cfg['dir']}/ -> _voices_backup/{voice_cfg['dir']}/")
            shutil.copytree(src, dst)


def restore_voices():
    """Restore all voice folders from backup."""
    if not os.path.isdir(VOICES_BACKUP_DIR):
        print("  Nenhum backup encontrado.")
        return
    for voice_key, voice_cfg in VOICES.items():
        src = os.path.join(VOICES_BACKUP_DIR, voice_cfg["dir"])
        dst = os.path.join(AUDIO_DIR, voice_cfg["dir"])
        if os.path.isdir(src) and not os.path.isdir(dst):
            print(f"  Restaurando: {voice_cfg['dir']}/")
            shutil.copytree(src, dst)
    print("  Todas as vozes restauradas.")


def prepare_data(voice_key):
    """Prepare data/ folder with only the selected voice."""
    voice_cfg = VOICES[voice_key]
    voice_dir_name = voice_cfg["dir"]
    source_voice_dir = os.path.join(AUDIO_DIR, voice_dir_name)

    if not os.path.isdir(source_voice_dir):
        # Check backup
        backup_voice = os.path.join(VOICES_BACKUP_DIR, voice_dir_name)
        if os.path.isdir(backup_voice):
            source_voice_dir = backup_voice
        else:
            print(f"ERRO: Pasta de voz '{voice_dir_name}' nao encontrada!")
            print(f"  Esperado em: {source_voice_dir}")
            print(f"  Ou backup em: {backup_voice}")
            print(f"  Execute generate_audio.py --voice {voice_key} primeiro.")
            return False

    file_count = count_files(source_voice_dir)
    if file_count == 0:
        print(f"ERRO: Pasta '{voice_dir_name}' esta vazia!")
        return False

    print(f"\n  Voz selecionada: {voice_cfg['label']}")
    print(f"  Arquivos de audio: {file_count}")
    print(f"  Tamanho: {get_dir_size_mb(source_voice_dir):.1f} MB")

    # Step 1: Backup all voices
    print("\n[1/4] Fazendo backup das vozes...")
    backup_voices()

    # Step 2: Clear audio directory (keep only selected voice)
    print("[2/4] Limpando pasta audio/...")
    if os.path.isdir(AUDIO_DIR):
        for item in os.listdir(AUDIO_DIR):
            item_path = os.path.join(AUDIO_DIR, item)
            if os.path.isdir(item_path):
                shutil.rmtree(item_path)

    # Step 3: Copy selected voice
    print(f"[3/4] Copiando voz '{voice_dir_name}'...")
    dest_voice_dir = os.path.join(AUDIO_DIR, voice_dir_name)

    # Copy from backup if source was removed
    if os.path.isdir(os.path.join(VOICES_BACKUP_DIR, voice_dir_name)):
        shutil.copytree(os.path.join(VOICES_BACKUP_DIR, voice_dir_name), dest_voice_dir)
    else:
        shutil.copytree(source_voice_dir, dest_voice_dir)

    # Step 4: Verify
    print("[4/4] Verificando...")

    audio_count = count_files(dest_voice_dir)
    gesture_count = count_files(GESTURES_DIR, ".json")
    audio_size = get_dir_size_mb(dest_voice_dir)
    gesture_size = get_dir_size_mb(GESTURES_DIR)
    total_size = audio_size + gesture_size

    print(f"\n  ={'=' * 50}")
    print(f"  Pasta data/ preparada para upload:")
    print(f"  ")
    print(f"  data/")
    print(f"    audio/{voice_dir_name}/    {audio_count} WAVs ({audio_size:.1f} MB)")
    print(f"    gestures/               {gesture_count} JSONs ({gesture_size:.2f} MB)")
    print(f"  ")
    print(f"  Total: {total_size:.1f} MB")
    print(f"  SPIFFS: 6.2 MB")
    print(f"  Margem: {6.2 - total_size:.1f} MB ({(6.2 - total_size) / 6.2 * 100:.0f}%)")
    print(f"  ")

    # FIX H16: Ordem invertida — checar erro (>6.2) ANTES do aviso (>6.0)
    # Antes: elif >6.2 era inalcancavel porque qualquer >6.2 tambem e >6.0
    if total_size > 6.2:
        print(f"  ERRO: Nao cabe no SPIFFS! Reduza os arquivos.")
        return False
    elif total_size > 6.0:
        print(f"  AVISO: Tamanho proximo do limite! Considere remover perfis.")
    else:
        print(f"  Status: OK - cabe no SPIFFS com folga")

    print(f"  ={'=' * 50}")
    return True


def run_upload():
    """Run PlatformIO SPIFFS upload."""
    print("\n  Iniciando upload SPIFFS...")
    print("  Comando: pio run -t uploadfs")
    print("  (conecte o Sensor A via USB-C)")
    print()

    try:
        result = subprocess.run(
            ["pio", "run", "-t", "uploadfs"],
            cwd=SENSOR_A_DIR,
            text=True,
        )
        if result.returncode == 0:
            print("\n  Upload SPIFFS concluido com sucesso!")
            return True
        else:
            print(f"\n  ERRO no upload (codigo {result.returncode})")
            print("  Verifique:")
            print("    - Sensor A esta conectado via USB-C?")
            print("    - Driver USB instalado?")
            print("    - PlatformIO CLI instalado? (pip install platformio)")
            return False
    except FileNotFoundError:
        print("\n  ERRO: 'pio' nao encontrado.")
        print("  Instale: pip install platformio")
        return False


def run_firmware_upload():
    """Run PlatformIO firmware upload."""
    print("\n  Iniciando upload do firmware...")
    print("  Comando: pio run -t upload")
    print()

    try:
        result = subprocess.run(
            ["pio", "run", "-t", "upload"],
            cwd=SENSOR_A_DIR,
            text=True,
        )
        if result.returncode == 0:
            print("\n  Firmware gravado com sucesso!")
            return True
        else:
            print(f"\n  ERRO no upload (codigo {result.returncode})")
            return False
    except FileNotFoundError:
        print("\n  ERRO: 'pio' nao encontrado.")
        return False


def interactive_menu():
    """Interactive voice selection menu."""
    print()
    print("=" * 55)
    print("  GESTUUM — Preparar Upload para Sensor A")
    print("=" * 55)
    print()

    # Show current state
    print("  Vozes disponiveis:")
    print()
    for i, (key, cfg) in enumerate(VOICES.items(), 1):
        voice_path = os.path.join(AUDIO_DIR, cfg["dir"])
        backup_path = os.path.join(VOICES_BACKUP_DIR, cfg["dir"])
        count = count_files(voice_path) or count_files(backup_path)
        size = get_dir_size_mb(voice_path) or get_dir_size_mb(backup_path)
        status = f"{count} WAVs, {size:.1f} MB" if count > 0 else "nao gerada"
        print(f"    {i}. {cfg['label']}")
        print(f"       ({status})")
        print()

    # Current data/ status
    current_voices_in_data = []
    if os.path.isdir(AUDIO_DIR):
        for item in os.listdir(AUDIO_DIR):
            if os.path.isdir(os.path.join(AUDIO_DIR, item)) and item in [v["dir"] for v in VOICES.values()]:
                current_voices_in_data.append(item)

    if current_voices_in_data:
        print(f"  Atualmente em data/audio/: {', '.join(current_voices_in_data)}")
    else:
        print(f"  Atualmente em data/audio/: (vazio)")

    print()
    print("  Opcoes:")
    print("    1-4  Selecionar voz e preparar upload")
    print("    r    Restaurar todas as vozes na pasta")
    print("    q    Sair")
    print()

    choice = input("  Escolha: ").strip().lower()

    if choice == "q":
        print("  Saindo.")
        return
    elif choice == "r":
        print("\n  Restaurando todas as vozes...")
        restore_voices()
        return

    voice_keys = list(VOICES.keys())
    try:
        idx = int(choice) - 1
        if 0 <= idx < len(voice_keys):
            selected = voice_keys[idx]
        else:
            print("  Opcao invalida.")
            return
    except ValueError:
        if choice in VOICES:
            selected = choice
        else:
            print("  Opcao invalida.")
            return

    if not prepare_data(selected):
        return

    print()
    upload_choice = input("  Fazer upload agora? (s/n): ").strip().lower()
    if upload_choice == "s":
        firmware_choice = input("  Gravar firmware tambem? (s/n): ").strip().lower()
        if firmware_choice == "s":
            run_firmware_upload()
        run_upload()

        print()
        restore_choice = input("  Restaurar todas as vozes na pasta? (s/n): ").strip().lower()
        if restore_choice == "s":
            restore_voices()
    else:
        print()
        print("  Pasta data/ preparada. Para fazer upload manualmente:")
        print(f"    cd {SENSOR_A_DIR}")
        print("    pio run -t upload      (firmware)")
        print("    pio run -t uploadfs    (dados SPIFFS)")
        print()
        print("  Para restaurar todas as vozes depois:")
        print(f"    python {os.path.basename(__file__)} --restore")


def main():
    parser = argparse.ArgumentParser(
        description="GESTUUM — Prepara SPIFFS para upload no Sensor A"
    )
    parser.add_argument(
        "--voice",
        choices=list(VOICES.keys()),
        help="Voz para upload (homem, mulher, menino, menina)",
    )
    parser.add_argument(
        "--upload",
        action="store_true",
        help="Fazer upload SPIFFS automaticamente apos preparar",
    )
    parser.add_argument(
        "--firmware",
        action="store_true",
        help="Gravar firmware tambem (junto com --upload)",
    )
    parser.add_argument(
        "--restore",
        action="store_true",
        help="Restaurar todas as vozes na pasta data/audio/",
    )
    parser.add_argument(
        "--sensor-b",
        action="store_true",
        help="Gravar firmware do Sensor B (nao precisa de SPIFFS)",
    )
    parser.add_argument(
        "--atoms3",
        action="store_true",
        help="Gravar firmware do AtomS3 LED (nao precisa de SPIFFS)",
    )

    args = parser.parse_args()

    if args.restore:
        print("  Restaurando todas as vozes...")
        restore_voices()
        return

    if args.sensor_b:
        print("  Gravando firmware Sensor B...")
        sensor_b_dir = os.path.join(PROJECT_ROOT, "sensor_b")
        subprocess.run(["pio", "run", "-t", "upload"], cwd=sensor_b_dir)
        return

    if args.atoms3:
        print("  Gravando firmware AtomS3 LED...")
        atoms3_dir = os.path.join(PROJECT_ROOT, "atoms3_led")
        subprocess.run(["pio", "run", "-t", "upload"], cwd=atoms3_dir)
        return

    if args.voice:
        if not prepare_data(args.voice):
            sys.exit(1)
        if args.upload:
            if args.firmware:
                run_firmware_upload()
            success = run_upload()
            # Restore after upload
            restore_voices()
            if not success:
                sys.exit(1)
    else:
        interactive_menu()


if __name__ == "__main__":
    main()
