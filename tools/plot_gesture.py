"""
GESTUUM — Visualizador de Trajetoria de Gestos
Gera imagem 3D da trajetoria real gravada no dispositivo.

Uso:
  python plot_gesture.py --port COM6 --gesture CX01
  python plot_gesture.py --file data/g/geral.json --id G01
  python plot_gesture.py --port COM6 --all
"""

import argparse
import json
import os
import sys

import matplotlib
matplotlib.use('Agg')  # Backend sem janela (salva direto em arquivo)
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
import numpy as np


def read_gesture_from_serial(port, gesture_id):
    """Le trajetoria de um gesto do dispositivo via Serial."""
    import serial
    import time

    ser = serial.Serial(port, 115200, timeout=2)
    time.sleep(0.5)
    ser.reset_input_buffer()

    # Pedir dump da trajetoria
    cmd = json.dumps({"cmd": "get_gesture", "gesture_id": gesture_id}) + "\n"
    ser.write(cmd.encode())
    ser.flush()
    time.sleep(2)

    data = ser.read(8192).decode('utf-8', errors='replace')
    ser.close()

    # Tentar parsear resposta JSON
    for line in data.split('\n'):
        line = line.strip()
        if line.startswith('{') and '"trajectory' in line:
            try:
                return json.loads(line)
            except:
                pass
    return None


def read_gesture_from_file(filepath, gesture_id):
    """Le trajetoria de um gesto de um arquivo JSON local."""
    with open(filepath, 'r') as f:
        data = json.load(f)

    if not isinstance(data, list):
        return None

    for item in data:
        # Contextos: id numerico
        if 'prefix' in item:
            cx_id = f"CX{item['id']:02d}"
            if cx_id == gesture_id:
                return {
                    'id': cx_id,
                    'name': item['prefix'],
                    'trajectory_a': item.get('trajectory', []),
                    'trained': 'sig_a' in item
                }
        # Gestos normais: id string
        elif item.get('id') == gesture_id:
            return {
                'id': gesture_id,
                'name': item.get('name', gesture_id),
                'trajectory_a': item.get('trajectory_a', []),
                'trajectory_b': item.get('trajectory_b', []),
                'trained': item.get('trained', False)
            }

    return None


def plot_trajectory(points, title, color='blue', ax=None, label=''):
    """Plota uma trajetoria 3D."""
    if not points or len(points) < 2:
        return

    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    zs = [p[2] for p in points]

    if ax is None:
        return

    # Linha da trajetoria com gradiente de cor (inicio→fim)
    n = len(xs)
    for i in range(n - 1):
        # Gradiente: azul escuro no inicio, vermelho no fim
        t = i / max(n - 1, 1)
        if color == 'blue':
            c = (t * 0.8, 0.2, 1.0 - t * 0.8, 0.8)
        else:
            c = (1.0 - t * 0.5, t * 0.8, 0.2, 0.8)
        ax.plot([xs[i], xs[i+1]], [ys[i], ys[i+1]], [zs[i], zs[i+1]],
                color=c, linewidth=2.5)

    # Ponto inicial (verde) e final (vermelho)
    ax.scatter(*points[0], color='green', s=100, zorder=5, label=f'{label} inicio')
    ax.scatter(*points[-1], color='red', s=100, zorder=5, marker='^', label=f'{label} fim')

    # Seta de direcao no ultimo segmento
    if n >= 2:
        dx = xs[-1] - xs[-2]
        dy = ys[-1] - ys[-2]
        dz = zs[-1] - zs[-2]
        ax.quiver(xs[-2], ys[-2], zs[-2], dx, dy, dz,
                  color='red', arrow_length_ratio=0.3, linewidth=2)


def generate_gesture_plot(gesture_data, output_path):
    """Gera imagem com visualizacao 3D + 3 projecoes 2D."""
    name = gesture_data.get('name', gesture_data.get('id', '?'))
    gid = gesture_data.get('id', '?')
    trained = gesture_data.get('trained', False)
    traj_a = gesture_data.get('trajectory_a', [])
    traj_b = gesture_data.get('trajectory_b', [])

    fig = plt.figure(figsize=(16, 12))
    fig.suptitle(f'GESTUUM — Trajetoria: {name} ({gid})\n'
                 f'{"TREINADO" if trained else "PLACEHOLDER"}',
                 fontsize=16, fontweight='bold')

    # Grid 11x11x11
    grid_max = 10
    grid_center = 5

    # === 3D principal (grande) ===
    ax3d = fig.add_subplot(2, 2, 1, projection='3d')
    ax3d.set_title('Vista 3D (cubo 11x11x11)', fontsize=12)

    # Desenhar cubo wireframe
    for s, e in [(0, grid_max)]:
        for i in [0, grid_max]:
            for j in [0, grid_max]:
                ax3d.plot([i, i], [j, j], [s, e], 'gray', alpha=0.1, linewidth=0.5)
                ax3d.plot([i, i], [s, e], [j, j], 'gray', alpha=0.1, linewidth=0.5)
                ax3d.plot([s, e], [i, i], [j, j], 'gray', alpha=0.1, linewidth=0.5)

    # Centro do cubo
    ax3d.scatter(grid_center, grid_center, grid_center,
                 color='gray', s=50, alpha=0.3, marker='x', label='centro')

    if traj_a:
        plot_trajectory(traj_a, name, 'blue', ax3d, 'Sensor A')
    if traj_b:
        plot_trajectory(traj_b, name, 'orange', ax3d, 'Sensor B')

    ax3d.set_xlabel('X')
    ax3d.set_ylabel('Y')
    ax3d.set_zlabel('Z')
    ax3d.set_xlim(0, grid_max)
    ax3d.set_ylim(0, grid_max)
    ax3d.set_zlim(0, grid_max)
    ax3d.legend(fontsize=8, loc='upper left')

    # === Projecao XY (vista de cima) ===
    ax_xy = fig.add_subplot(2, 2, 2)
    ax_xy.set_title('Vista Superior (X-Y)', fontsize=12)
    ax_xy.set_xlabel('X (lateral)')
    ax_xy.set_ylabel('Y (frente/tras)')
    ax_xy.set_xlim(-0.5, grid_max + 0.5)
    ax_xy.set_ylim(-0.5, grid_max + 0.5)
    ax_xy.set_aspect('equal')
    ax_xy.grid(True, alpha=0.2)
    ax_xy.axhline(y=grid_center, color='gray', linestyle='--', alpha=0.3)
    ax_xy.axvline(x=grid_center, color='gray', linestyle='--', alpha=0.3)

    for traj, color, label in [(traj_a, 'blue', 'A'), (traj_b, 'orange', 'B')]:
        if traj and len(traj) >= 2:
            xs = [p[0] for p in traj]
            ys = [p[1] for p in traj]
            ax_xy.plot(xs, ys, '-o', color=color, markersize=4, linewidth=2, alpha=0.7, label=label)
            ax_xy.scatter(xs[0], ys[0], color='green', s=80, zorder=5)
            ax_xy.scatter(xs[-1], ys[-1], color='red', s=80, zorder=5, marker='^')
    ax_xy.legend(fontsize=8)

    # === Projecao XZ (vista lateral) ===
    ax_xz = fig.add_subplot(2, 2, 3)
    ax_xz.set_title('Vista Lateral (X-Z)', fontsize=12)
    ax_xz.set_xlabel('X (lateral)')
    ax_xz.set_ylabel('Z (cima/baixo)')
    ax_xz.set_xlim(-0.5, grid_max + 0.5)
    ax_xz.set_ylim(-0.5, grid_max + 0.5)
    ax_xz.set_aspect('equal')
    ax_xz.grid(True, alpha=0.2)
    ax_xz.axhline(y=grid_center, color='gray', linestyle='--', alpha=0.3)
    ax_xz.axvline(x=grid_center, color='gray', linestyle='--', alpha=0.3)

    for traj, color, label in [(traj_a, 'blue', 'A'), (traj_b, 'orange', 'B')]:
        if traj and len(traj) >= 2:
            xs = [p[0] for p in traj]
            zs = [p[2] for p in traj]
            ax_xz.plot(xs, zs, '-o', color=color, markersize=4, linewidth=2, alpha=0.7, label=label)
            ax_xz.scatter(xs[0], zs[0], color='green', s=80, zorder=5)
            ax_xz.scatter(xs[-1], zs[-1], color='red', s=80, zorder=5, marker='^')
    ax_xz.legend(fontsize=8)

    # === Projecao YZ (vista frontal) ===
    ax_yz = fig.add_subplot(2, 2, 4)
    ax_yz.set_title('Vista Frontal (Y-Z)', fontsize=12)
    ax_yz.set_xlabel('Y (frente/tras)')
    ax_yz.set_ylabel('Z (cima/baixo)')
    ax_yz.set_xlim(-0.5, grid_max + 0.5)
    ax_yz.set_ylim(-0.5, grid_max + 0.5)
    ax_yz.set_aspect('equal')
    ax_yz.grid(True, alpha=0.2)
    ax_yz.axhline(y=grid_center, color='gray', linestyle='--', alpha=0.3)
    ax_yz.axvline(x=grid_center, color='gray', linestyle='--', alpha=0.3)

    for traj, color, label in [(traj_a, 'blue', 'A'), (traj_b, 'orange', 'B')]:
        if traj and len(traj) >= 2:
            ys = [p[1] for p in traj]
            zs = [p[2] for p in traj]
            ax_yz.plot(ys, zs, '-o', color=color, markersize=4, linewidth=2, alpha=0.7, label=label)
            ax_yz.scatter(ys[0], zs[0], color='green', s=80, zorder=5)
            ax_yz.scatter(ys[-1], zs[-1], color='red', s=80, zorder=5, marker='^')
    ax_yz.legend(fontsize=8)

    # Info
    info = f'Pontos A: {len(traj_a)} | Pontos B: {len(traj_b)}'
    fig.text(0.5, 0.02, info, ha='center', fontsize=10, color='gray')

    plt.tight_layout(rect=[0, 0.04, 1, 0.95])
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    plt.close()
    print(f'Salvo: {output_path}')


def main():
    parser = argparse.ArgumentParser(description='Visualizador de trajetoria de gestos GESTUUM')
    parser.add_argument('--file', help='Arquivo JSON local (geral.json, contexts.json)')
    parser.add_argument('--id', help='ID do gesto (G01, CX01, etc.)')
    parser.add_argument('--port', help='Porta serial do dispositivo (COM6)')
    parser.add_argument('--all', action='store_true', help='Plotar todos os gestos do arquivo')
    parser.add_argument('--output', default='docs/gestures', help='Pasta de saida (default: docs/gestures)')
    args = parser.parse_args()

    os.makedirs(args.output, exist_ok=True)

    if args.file:
        if args.all:
            # Plotar todos do arquivo
            with open(args.file, 'r') as f:
                data = json.load(f)
            for item in data:
                if 'prefix' in item:
                    gid = f"CX{item['id']:02d}"
                    gesture = read_gesture_from_file(args.file, gid)
                else:
                    gid = item.get('id', '?')
                    gesture = read_gesture_from_file(args.file, gid)
                if gesture:
                    outfile = os.path.join(args.output, f'{gid}_{gesture["name"]}.png')
                    generate_gesture_plot(gesture, outfile)
        elif args.id:
            gesture = read_gesture_from_file(args.file, args.id)
            if gesture:
                outfile = os.path.join(args.output, f'{args.id}_{gesture["name"]}.png')
                generate_gesture_plot(gesture, outfile)
            else:
                print(f'Gesto {args.id} nao encontrado em {args.file}')
    elif args.port and args.id:
        gesture = read_gesture_from_serial(args.port, args.id)
        if gesture:
            outfile = os.path.join(args.output, f'{args.id}.png')
            generate_gesture_plot(gesture, outfile)
        else:
            print(f'Nao foi possivel ler {args.id} do dispositivo')
    else:
        parser.print_help()


if __name__ == '__main__':
    main()
