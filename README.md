# GESTUUM — Sua voz não precisa de palavras.

Dispositivo vestível que transforma gestos em fala. Sem celular, sem internet. Autonomia para pessoas que se comunicam de forma diferente.

**Site:** [gestuum.com](https://gestuum.com)
**Projeto:** Science Fair COREE 2026 — Programa IB

---

## O que é

O GESTUUM é um sistema de tecnologia assistiva que reconhece gestos das mãos e os converte em fala em tempo real. Dois sensores vestíveis capturam o movimento, um motor de reconhecimento identifica o gesto e o áudio é reproduzido pelo alto-falante integrado ou por uma caixa de som Bluetooth.

Projetado para pessoas com dificuldade de fala — afasia, paralisia cerebral, ELA, autismo não-verbal, pós-AVC — o GESTUUM devolve autonomia de comunicação sem depender de telas, teclados ou conexão com internet.

## Como funciona

1. **Faça o gesto** — O sensor na mão capta aceleração e rotação a 50Hz
2. **O GESTUUM entende** — O motor Orbital extrai a assinatura do movimento em <1ms
3. **Sua voz é ouvida** — O áudio toca pelo speaker integrado e/ou Bluetooth

### Combine gestos para formar frases

| Mão esquerda (contexto) | Mão direita (objeto) | Frase |
|---|---|---|
| Cozinha | Água | "Quero água" |
| Escola | Ajuda | "Preciso de ajuda" |
| Dor | Cabeça | "Estou com dor de cabeça" |

## Hardware

Dois sensores vestíveis:

| Dispositivo | Modelo | Função |
|---|---|---|
| Sensor Principal | M5StickC Plus2 + HAT-SPK2 | IMU + display + áudio + reconhecimento de gestos |
| Sensor Secundário | M5StickC Plus2 | IMU da mão auxiliar, transmite via ESP-NOW |

**Custo do kit completo:** ~R$ 600

## Funcionalidades

- **6 idiomas** — Português, English, Español, Français, 中文, العربية
- **4 perfis de voz** — Homem, Mulher, Menino, Menina
- **210+ frases por idioma** — Vocabulário expansível
- **53 gestos carregados** — Treináveis pelo usuário
- **Bluetooth A2DP** — Streaming para caixa de som externa
- **Configuração via smartphone** — App BLE para treino e ajustes
- **100% sem fio** — ESP-NOW entre sensores (latência <5ms)
- **Sem internet** — Tudo roda local no dispositivo

## Stack técnico

| Componente | Tecnologia |
|---|---|
| SoC | ESP32-PICO-V3-02 (dual-core 240MHz, 8MB Flash, 2MB PSRAM) |
| Framework | Arduino via PlatformIO (espressif32@6.12.0) |
| Linguagem | C++ (firmware), Python (tools), HTML/CSS/JS (instalador/configurador) |
| Comunicação | ESP-NOW (canal 13), BLE GATT, Bluetooth A2DP |
| Áudio | I2S via M5.Speaker, WAV 48kHz, SPIFFS 5.44MB |
| Reconhecimento | Modelo Orbital (8 parâmetros) + Grid 11x11x11 + DTW |

## Estrutura do repositório

```
sensor_a/       Firmware principal (gestos + áudio + display)
sensor_b/       Firmware secundário (IMU via ESP-NOW)
shared/         Bibliotecas compartilhadas (protocol, constants, matrix3d, dtw)
app/            Instalador (gravar firmware via USB) + configurador (BLE)
tools/          Scripts Python (geração de áudio, backup SPIFFS)
Documentos/     Documentação técnica e datasheets de hardware
```

## Build

Requer [PlatformIO](https://platformio.org/) instalado.

```bash
# Compilar Sensor A
cd sensor_a && pio run

# Compilar Sensor B
cd sensor_b && pio run

# Upload firmware (Sensor A na COM6, Sensor B na COM3)
cd sensor_a && pio run -t upload --upload-port COM6
cd sensor_b && pio run -t upload --upload-port COM3

# Upload áudios para SPIFFS (ATENÇÃO: apaga tudo no SPIFFS!)
cd sensor_a && pio run -t uploadfs --upload-port COM6
```

> **IMPORTANTE:** Sempre faça backup do SPIFFS antes de `uploadfs` — o comando apaga todos os arquivos (WAVs e JSONs de gestos).

## Instalação sem compilar

O firmware pré-compilado e o instalador web (esp-web-tools) ficam em `app/` — basta servir a pasta e gravar via cabo USB-C direto do navegador (Chrome/Edge).

## Licença

Código-fonte licenciado sob **Apache License 2.0**. Veja [LICENSE](LICENSE) e [NOTICE](NOTICE).

Copyright 2026 Alexandre Jalkh.

Projeto acadêmico — Science Fair COREE 2026, Programa IB.
