"""
GESTUUM — Backup e restauracao do SPIFFS (gestos treinados + WAVs).

Uso:
    python backup_spiffs.py --backup                 # Salva SPIFFS em _backups/
    python backup_spiffs.py --backup --name meu_bkp  # Salva com nome especifico
    python backup_spiffs.py --restore ultimo          # Restaura ultimo backup
    python backup_spiffs.py --restore meu_bkp        # Restaura backup especifico
    python backup_spiffs.py --list                   # Lista backups disponiveis
    python backup_spiffs.py --port COM6              # Especificar porta (default: COM6)

Partição SPIFFS: offset=0x290000, size=0x570000 (5.44MB)
"""

import argparse
import os
import subprocess
import sys
import time
from datetime import datetime

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
BACKUP_DIR = os.path.join(SCRIPT_DIR, "_backups")

# Partição SPIFFS do GESTUUM (partitions.csv)
SPIFFS_OFFSET = 0x290000
SPIFFS_SIZE = 0x570000

# esptool path — usa o do PlatformIO se disponivel
ESPTOOL = "esptool.py"


def find_esptool():
    """Encontra o esptool.py no PATH ou no PlatformIO."""
    # Tentar no PATH
    try:
        subprocess.run([ESPTOOL, "version"], capture_output=True, timeout=5)
        return ESPTOOL
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    # Tentar via python -m esptool
    try:
        subprocess.run([sys.executable, "-m", "esptool", "version"],
                       capture_output=True, timeout=5)
        return [sys.executable, "-m", "esptool"]
    except (FileNotFoundError, subprocess.TimeoutExpired):
        pass

    print("ERRO: esptool.py nao encontrado. Instale com: pip install esptool")
    sys.exit(1)


def do_backup(port, name=None):
    """Faz backup da particao SPIFFS do dispositivo."""
    os.makedirs(BACKUP_DIR, exist_ok=True)

    if name is None:
        name = datetime.now().strftime("%Y%m%d_%H%M%S")

    filename = f"spiffs_{name}.bin"
    filepath = os.path.join(BACKUP_DIR, filename)

    if os.path.exists(filepath):
        print(f"AVISO: {filename} ja existe. Sobrescrever? (s/n)")
        if input().strip().lower() != 's':
            print("Cancelado.")
            return

    esptool = find_esptool()
    cmd = esptool if isinstance(esptool, list) else [esptool]
    cmd += [
        "--port", port,
        "--baud", "921600",
        "read_flash",
        hex(SPIFFS_OFFSET),
        hex(SPIFFS_SIZE),
        filepath,
    ]

    print(f"Fazendo backup do SPIFFS -> {filename}")
    print(f"  Porta: {port}")
    print(f"  Offset: {hex(SPIFFS_OFFSET)}, Size: {hex(SPIFFS_SIZE)}")
    print()

    result = subprocess.run(cmd, capture_output=False)

    if result.returncode == 0:
        size_mb = os.path.getsize(filepath) / (1024 * 1024)
        print(f"\nBackup salvo: {filepath} ({size_mb:.1f} MB)")
    else:
        print(f"\nERRO no backup (exit code {result.returncode})")


def do_restore(port, name):
    """Restaura um backup da particao SPIFFS."""
    if name == "ultimo":
        # Encontrar o backup mais recente
        files = [f for f in os.listdir(BACKUP_DIR) if f.startswith("spiffs_") and f.endswith(".bin")]
        if not files:
            print("Nenhum backup encontrado.")
            return
        files.sort(reverse=True)
        filename = files[0]
    else:
        filename = f"spiffs_{name}.bin" if not name.endswith(".bin") else name

    filepath = os.path.join(BACKUP_DIR, filename)
    if not os.path.exists(filepath):
        print(f"Backup nao encontrado: {filepath}")
        return

    esptool = find_esptool()
    cmd = esptool if isinstance(esptool, list) else [esptool]
    cmd += [
        "--port", port,
        "--baud", "921600",
        "write_flash",
        hex(SPIFFS_OFFSET),
        filepath,
    ]

    size_mb = os.path.getsize(filepath) / (1024 * 1024)
    print(f"Restaurando SPIFFS <- {filename} ({size_mb:.1f} MB)")
    print(f"  Porta: {port}")
    print()

    result = subprocess.run(cmd, capture_output=False)

    if result.returncode == 0:
        print(f"\nRestaurado com sucesso. Reinicie o dispositivo.")
    else:
        print(f"\nERRO na restauracao (exit code {result.returncode})")


def do_list():
    """Lista backups disponiveis."""
    if not os.path.exists(BACKUP_DIR):
        print("Nenhum backup encontrado.")
        return

    files = [f for f in os.listdir(BACKUP_DIR) if f.startswith("spiffs_") and f.endswith(".bin")]
    if not files:
        print("Nenhum backup encontrado.")
        return

    files.sort(reverse=True)
    print(f"Backups em {BACKUP_DIR}:\n")
    for f in files:
        size_mb = os.path.getsize(os.path.join(BACKUP_DIR, f)) / (1024 * 1024)
        print(f"  {f}  ({size_mb:.1f} MB)")

    print(f"\nTotal: {len(files)} backup(s)")


def main():
    parser = argparse.ArgumentParser(description="Backup/restauracao do SPIFFS do GESTUUM")
    parser.add_argument("--backup", action="store_true", help="Fazer backup do SPIFFS")
    parser.add_argument("--restore", type=str, help="Restaurar backup (nome ou 'ultimo')")
    parser.add_argument("--list", action="store_true", help="Listar backups disponiveis")
    parser.add_argument("--name", type=str, help="Nome do backup (default: timestamp)")
    parser.add_argument("--port", default="COM6", help="Porta serial (default: COM6)")
    args = parser.parse_args()

    if args.list:
        do_list()
    elif args.backup:
        do_backup(args.port, args.name)
    elif args.restore:
        do_restore(args.port, args.restore)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
