"""
GESTUUM — Visualizador 3D Interativo de Gestos
Gera HTML com cubo 3D rotativo no navegador.

Uso:
  python plot_gesture_3d.py --file sensor_a/data/g/contexts.json --all
  python plot_gesture_3d.py --file sensor_a/data/g/geral.json --id G01
"""

import argparse
import json
import os
import plotly.graph_objects as go


def load_gestures_from_file(filepath):
    """Carrega todos os gestos de um arquivo JSON."""
    with open(filepath, 'r') as f:
        data = json.load(f)

    gestures = []
    for item in data:
        if 'prefix' in item:
            # Contexto
            gestures.append({
                'id': f"CX{item['id']:02d}",
                'name': item['prefix'],
                'trajectory_a': item.get('trajectory', []),
                'trajectory_b': [],
                'trained': 'sig_a' in item
            })
        else:
            # Gesto normal
            gestures.append({
                'id': item.get('id', '?'),
                'name': item.get('name', '?'),
                'trajectory_a': item.get('trajectory_a', []),
                'trajectory_b': item.get('trajectory_b', []),
                'trained': item.get('trained', False)
            })
    return gestures


def create_3d_figure(gestures, title="GESTUUM — Trajetorias 3D"):
    """Cria figura 3D interativa com todos os gestos."""
    fig = go.Figure()

    grid_max = 10
    grid_center = 5

    # Cubo wireframe (arestas)
    edges = []
    for i in [0, grid_max]:
        for j in [0, grid_max]:
            edges.append(([i, i], [j, j], [0, grid_max]))
            edges.append(([i, i], [0, grid_max], [j, j]))
            edges.append(([0, grid_max], [i, i], [j, j]))

    for ex, ey, ez in edges:
        fig.add_trace(go.Scatter3d(
            x=ex, y=ey, z=ez,
            mode='lines', line=dict(color='gray', width=1),
            showlegend=False, hoverinfo='skip', opacity=0.2
        ))

    # Centro
    fig.add_trace(go.Scatter3d(
        x=[grid_center], y=[grid_center], z=[grid_center],
        mode='markers', marker=dict(size=4, color='gray', symbol='x'),
        name='Centro (5,5,5)', hoverinfo='name'
    ))

    # Cores para cada gesto
    colors_a = ['#1f77b4', '#ff7f0e', '#2ca02c', '#d62728', '#9467bd',
                '#8c564b', '#e377c2', '#7f7f7f', '#bcbd22', '#17becf',
                '#1a9850', '#d73027', '#4575b4', '#91bfdb', '#fee090']
    colors_b = ['#aec7e8', '#ffbb78', '#98df8a', '#ff9896', '#c5b0d5',
                '#c49c94', '#f7b6d2', '#c7c7c7', '#dbdb8d', '#9edae5']

    for idx, g in enumerate(gestures):
        color_a = colors_a[idx % len(colors_a)]
        color_b = colors_b[idx % len(colors_b)]
        trained_tag = " ✓" if g['trained'] else ""
        label = f"{g['id']} {g['name']}{trained_tag}"

        # Trajetoria A (sensor principal)
        traj_a = g.get('trajectory_a', [])
        if traj_a and len(traj_a) >= 2:
            xs = [p[0] for p in traj_a]
            ys = [p[1] for p in traj_a]
            zs = [p[2] for p in traj_a]

            # Linha
            fig.add_trace(go.Scatter3d(
                x=xs, y=ys, z=zs,
                mode='lines+markers',
                line=dict(color=color_a, width=5),
                marker=dict(size=3, color=color_a),
                name=f'{label} (A)',
                hovertext=[f'{label}<br>Ponto {i}<br>({xs[i]},{ys[i]},{zs[i]})' for i in range(len(xs))],
                hoverinfo='text'
            ))

            # Inicio (verde grande)
            fig.add_trace(go.Scatter3d(
                x=[xs[0]], y=[ys[0]], z=[zs[0]],
                mode='markers', marker=dict(size=8, color='green', symbol='circle'),
                name=f'{g["id"]} inicio', showlegend=False,
                hovertext=f'{label}<br>INICIO ({xs[0]},{ys[0]},{zs[0]})', hoverinfo='text'
            ))

            # Fim (vermelho triangulo)
            fig.add_trace(go.Scatter3d(
                x=[xs[-1]], y=[ys[-1]], z=[zs[-1]],
                mode='markers', marker=dict(size=8, color='red', symbol='diamond'),
                name=f'{g["id"]} fim', showlegend=False,
                hovertext=f'{label}<br>FIM ({xs[-1]},{ys[-1]},{zs[-1]})', hoverinfo='text'
            ))

        # Trajetoria B
        traj_b = g.get('trajectory_b', [])
        if traj_b and len(traj_b) >= 2:
            xs = [p[0] for p in traj_b]
            ys = [p[1] for p in traj_b]
            zs = [p[2] for p in traj_b]

            fig.add_trace(go.Scatter3d(
                x=xs, y=ys, z=zs,
                mode='lines+markers',
                line=dict(color=color_b, width=3, dash='dash'),
                marker=dict(size=2, color=color_b),
                name=f'{label} (B)',
                hovertext=[f'{label} B<br>Ponto {i}<br>({xs[i]},{ys[i]},{zs[i]})' for i in range(len(xs))],
                hoverinfo='text'
            ))

    fig.update_layout(
        title=dict(text=title, font=dict(size=18)),
        scene=dict(
            xaxis=dict(title='X (lateral)', range=[0, grid_max]),
            yaxis=dict(title='Y (frente/tras)', range=[0, grid_max]),
            zaxis=dict(title='Z (cima/baixo)', range=[0, grid_max]),
            aspectmode='cube'
        ),
        legend=dict(x=0, y=1, font=dict(size=10)),
        width=1000, height=800,
        margin=dict(l=0, r=0, b=0, t=40)
    )

    return fig


def main():
    parser = argparse.ArgumentParser(description='Visualizador 3D interativo de gestos GESTUUM')
    parser.add_argument('--file', nargs='+', help='Arquivo(s) JSON (geral.json, contexts.json)')
    parser.add_argument('--id', help='Filtrar por ID do gesto')
    parser.add_argument('--output', default='docs/gestures/3d_viewer.html', help='Arquivo HTML de saida')
    parser.add_argument('--all', action='store_true', help='Incluir todos os gestos')
    args = parser.parse_args()

    if not args.file:
        parser.print_help()
        return

    all_gestures = []
    for f in args.file:
        all_gestures.extend(load_gestures_from_file(f))

    if args.id:
        all_gestures = [g for g in all_gestures if g['id'] == args.id]

    if not all_gestures:
        print('Nenhum gesto encontrado')
        return

    os.makedirs(os.path.dirname(args.output) or '.', exist_ok=True)

    title = "GESTUUM — Trajetorias 3D"
    if args.id:
        title += f" ({args.id})"
    else:
        title += f" ({len(all_gestures)} gestos)"

    fig = create_3d_figure(all_gestures, title)
    fig.write_html(args.output)
    print(f'Salvo: {args.output}')
    print(f'Abra no navegador para rotacionar o cubo 3D')


if __name__ == '__main__':
    main()
