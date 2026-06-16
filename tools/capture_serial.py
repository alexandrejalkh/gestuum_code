"""Captura log serial com timestamp. Roda em background."""
import serial, sys, time, os
# FIX 2026-05-01: forca stdout em UTF-8 pra evitar UnicodeEncodeError
# em Windows (console default cp1252 quebra com acentos/unicode do M5).
try:
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
except Exception:
    pass

port = sys.argv[1] if len(sys.argv) > 1 else 'COM6'
baud = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
out_path = sys.argv[3] if len(sys.argv) > 3 else f'/tmp/serial_{port}.log'

print(f'[capture_serial] {port} @ {baud} -> {out_path}', flush=True)
try:
    ser = serial.Serial(port, baud, timeout=0.5)
except Exception as e:
    print(f'ERRO ao abrir {port}: {e}', flush=True)
    sys.exit(1)

# trunca arquivo
with open(out_path, 'w', encoding='utf-8') as f:
    f.write('')

t0 = time.time()
last_flush = t0
buf = []
try:
    while True:
        line = ser.readline()
        if not line:
            continue
        try:
            text = line.decode('utf-8', errors='replace').rstrip()
        except Exception:
            text = repr(line)
        ts = f'{time.time()-t0:7.2f}'
        out = f'[{ts}] {text}'
        print(out, flush=True)
        buf.append(out)
        if time.time() - last_flush > 0.5:
            with open(out_path, 'a', encoding='utf-8') as f:
                f.write('\n'.join(buf) + '\n')
            buf = []
            last_flush = time.time()
except KeyboardInterrupt:
    if buf:
        with open(out_path, 'a', encoding='utf-8') as f:
            f.write('\n'.join(buf) + '\n')
    print('[capture_serial] stopped', flush=True)
    ser.close()
