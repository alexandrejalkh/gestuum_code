"""
GESTUUM — Compartilhar App com Celular
Serve o web app na rede WiFi local e mostra QR code para o celular acessar.

Como usar:
    1. Conecte o PC e o celular na mesma rede WiFi
    2. Execute: python share_app.py
    3. Escaneie o QR code com a camera do celular
    4. App abre no Chrome
    5. No Chrome: menu (3 pontos) > "Adicionar a tela inicial"

O app funciona offline depois de abrir a primeira vez.
"""

import http.server
import os
import socket
import sys
import threading
import webbrowser


SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.dirname(SCRIPT_DIR)
SERVE_DIR = PROJECT_ROOT
PORT = 8080


def get_local_ip():
    """Get the local network IP address."""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def generate_qr_terminal(url):
    """Generate a QR code in the terminal using Unicode block characters."""
    try:
        import qrcode
        qr = qrcode.QRCode(version=1, box_size=1, border=2,
                           error_correction=qrcode.constants.ERROR_CORRECT_L)
        qr.add_data(url)
        qr.make(fit=True)

        matrix = qr.get_matrix()
        lines = []
        for r in range(0, len(matrix) - 1, 2):
            line = ""
            for c in range(len(matrix[r])):
                top = matrix[r][c]
                bot = matrix[r + 1][c] if r + 1 < len(matrix) else False
                if top and bot:
                    line += "\u2588"      # Full block
                elif top and not bot:
                    line += "\u2580"      # Upper half
                elif not top and bot:
                    line += "\u2584"      # Lower half
                else:
                    line += " "
            lines.append(line)
        return "\n".join(lines)
    except ImportError:
        return None


def generate_qr_simple(url):
    """Fallback QR code using a simple ASCII representation."""
    # If qrcode library not available, just show the URL prominently
    border = "=" * 50
    return f"""
{border}

  QR code nao disponivel (instale: pip install qrcode)

  Abra este link no celular:

  {url}

  Ou compartilhe via WhatsApp/Email

{border}"""


def main():
    ip = get_local_ip()
    url = f"http://{ip}:{PORT}/app/gestuum-app.html"

    # Try to generate QR code
    qr_text = generate_qr_terminal(url)
    if qr_text is None:
        qr_text = generate_qr_simple(url)
        # Try to install qrcode
        print("  Instalando biblioteca QR code...")
        os.system(f"{sys.executable} -m pip install qrcode -q")
        qr_text = generate_qr_terminal(url)
        if qr_text is None:
            qr_text = generate_qr_simple(url)

    # Clear screen
    os.system("cls" if os.name == "nt" else "clear")

    print()
    print("  " + "=" * 50)
    print("  GESTUUM — Compartilhar App com Celular")
    print("  " + "=" * 50)
    print()
    print("  1. PC e celular devem estar na mesma rede WiFi")
    print("  2. Escaneie o QR code com a camera do celular")
    print("  3. App abre no Chrome automaticamente")
    print()
    print("  " + "-" * 50)
    print()

    # Show QR code
    for line in qr_text.split("\n"):
        print("    " + line)

    print()
    print("  " + "-" * 50)
    print()
    print(f"  URL: {url}")
    print(f"  IP local: {ip}")
    print(f"  Porta: {PORT}")
    print()
    print("  Dica: No Chrome do celular, va em")
    print("  Menu (3 pontos) > 'Adicionar a tela inicial'")
    print("  para instalar como app.")
    print()
    print("  Pressione Ctrl+C para encerrar o servidor.")
    print("  " + "=" * 50)
    print()

    # Also open in local browser
    webbrowser.open(url)

    # Start HTTP server
    os.chdir(SERVE_DIR)
    handler = http.server.SimpleHTTPRequestHandler

    # Suppress request logs for cleaner output
    class QuietHandler(handler):
        def log_message(self, format, *args):
            pass

    try:
        server = http.server.HTTPServer(("0.0.0.0", PORT), QuietHandler)
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  Servidor encerrado.")
    except OSError as e:
        if "Address already in use" in str(e) or "10048" in str(e):
            print(f"\n  ERRO: Porta {PORT} ja esta em uso.")
            print(f"  Tente: python share_app.py")
            print(f"  Ou acesse diretamente: {url}")
        else:
            raise


if __name__ == "__main__":
    main()
