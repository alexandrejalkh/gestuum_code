# M5StickC PLUS2 — Documentacao Tecnica Completa

> Biblioteca de referencia para o projeto GESTUUM
> Compilada em: 2026-04-04
> Fontes: 10 PDFs tecnicos (512 paginas), datasheets oficiais

---

## Indice

1. [Hardware — Visao Geral](#1-hardware)
2. [Power Management — GPIO4 HOLD (CRITICO)](#2-power-management)
3. [Botoes](#3-botoes)
4. [Boot e Recovery](#4-boot-e-recovery)
5. [Comunicacao (I2C, SPI, Serial)](#5-comunicacao)
6. [Programacao (M5Unified, PlatformIO)](#6-programacao)
7. [ESP32-PICO-V3-02](#7-esp32-pico)
8. [MPU-6886 (IMU)](#8-mpu-6886)
9. [BM8563 (RTC)](#9-bm8563)
10. [SPM1423 (Microfone)](#10-spm1423)
11. [ST7789V (Display LCD)](#11-st7789v)
12. [Problemas Conhecidos e Solucoes](#12-problemas)

---

## 1. Hardware

### Especificacoes Gerais

| Componente | Especificacao |
|------------|---------------|
| SoC | ESP32-PICO-V3-02 (dual-core Xtensa LX6, 240 MHz) |
| Flash | 8 MB (interna ao SiP) |
| PSRAM | 2 MB (interna ao SiP) |
| SRAM | 520 KB |
| Display | TFT 1.14" 135x240 pixels, ST7789V2, SPI |
| IMU | MPU6886 (acelerometro + giroscopio 6 eixos) |
| RTC | BM8563 (relogio tempo real) |
| Microfone | SPM1423HM4H-B (PDM MEMS) |
| USB-Serial | CH9102F |
| Bateria | Li-Po 200 mAh |
| Buzzer | Passivo, GPIO2 (PWM) |
| LED | Vermelho, GPIO19 (compartilhado com IR) |
| IR | Emissor infravermelho, GPIO19 |
| Botoes | BtnA (GPIO37), BtnB (GPIO39), BtnC (Power) |
| Grove | HY2.0-4P: GND, 5V, GPIO32, GPIO33 |

### Comparacao Plus vs Plus2

| Caracteristica | M5StickC Plus (v1) | M5StickC PLUS2 |
|---------------|-------------------|----------------|
| SoC | ESP32-PICO-D4 | ESP32-PICO-V3-02 |
| Flash | 4 MB | 8 MB |
| PSRAM | Ausente | 2 MB |
| PMIC | AXP192 (chip dedicado) | **REMOVIDO** — Power Latch via GPIO4 (sem PMIC nenhum!) |
| Bateria | 120 mAh | 200 mAh |
| USB-Serial | CH552 | CH9102 |
| Power On | Botao C (2s) | Botao C + GPIO4 HOLD no firmware |

**IMPACTO CRITICO:** O PLUS2 NAO tem PMIC nenhum (nem AXP192 nem AXP2101).
O firmware DEVE setar GPIO4 HIGH no inicio do setup(), senao o dispositivo
desliga ao soltar o botao power. A M5Unified faz isso automaticamente no begin().

**NOTA:** O LED verde que acende ao segurar o botao power NAO e programavel.
E indicador de hardware atrelado ao circuito de carga/power.

### Pinout Completo

| GPIO | Funcao | Tipo | Restricoes |
|------|--------|------|-----------|
| 0 | Microfone CLK (PDM) | I/O | **STRAPPING PIN** — conflito com boot mode! |
| 1 | USB TX (CH9102) | Output | Reservado para Serial |
| 2 | Buzzer (PWM) | I/O | **STRAPPING PIN** |
| 3 | USB RX (CH9102) | Input | Reservado para Serial |
| 4 | **POWER HOLD** | Output | **CRITICO** — deve ser HIGH para manter energia |
| 5 | Display CS | Output | SPI Chip Select |
| 12 | Display RST | Output | **STRAPPING PIN** — tensao flash |
| 13 | Display SCK | Output | SPI Clock |
| 14 | Display DC | Output | Data/Command |
| 15 | Display MOSI | Output | **STRAPPING PIN** — SPI Data |
| 19 | LED vermelho / IR TX | Output | Logica invertida (LOW liga) |
| 21 | I2C SDA (interno) | I/O | IMU + RTC |
| 22 | I2C SCL (interno) | I/O | IMU + RTC |
| 25 | I2S DAC | Output | HAT-SPK2 audio |
| 26 | I2S BCLK / DAC2 | I/O | HAT-SPK2 audio |
| 27 | Display Backlight | Output | PWM brightness |
| 32 | Grove SDA / GPIO | I/O | I2C externo ou GPIO |
| 33 | Grove SCL / GPIO | I/O | I2C externo ou GPIO |
| 34 | Microfone DATA (PDM) | Input | **Input-only** (sem pull-up/down) |
| 36 | Sem uso exposto | Input | **Input-only**, ADC chattering com WiFi |
| 37 | **Botao A** (frontal) | Input | **Input-only**, pull-up, logica invertida |
| 38 | Bateria ADC | Input | **Input-only**, divisor 2:1, leitura tensao |
| 39 | **Botao B** (lateral) | Input | **Input-only**, ADC chattering com WiFi |

### Restricoes Criticas dos GPIOs

**Input-only (sem output):** GPIO34, 35, 36, 37, 38, 39
- Nao tem pull-up/pull-down interno
- GPIO36 e GPIO39: sofrem pulsos fantasma quando ADC WiFi esta ativo (errata ESP32)

**Strapping Pins (afetam boot):**
- GPIO0: HIGH = boot normal, LOW = download mode. Compartilhado com microfone!
- GPIO2: deve estar floating ou LOW no boot
- GPIO12: controla tensao da flash (deve ser LOW para 3.3V flash)
- GPIO15: controla log do UART0 no boot

---

## 2. Power Management (CRITICO)

### Power Latch — GPIO4 HOLD

O M5StickC PLUS2 **NAO tem PMIC dedicado** (removido o AXP192 do modelo anterior).
A energia e mantida por um circuito de retencao passiva (Power Latch):

```
Botao Power pressionado → Energia flui momentaneamente → ESP32 liga
  → Firmware DEVE setar GPIO4 = HIGH → Transistor fecha → Energia mantida
  → Se GPIO4 nao for setado → Energia corta ao soltar o botao → "nao liga"
```

**Codigo obrigatorio no setup():**

```cpp
// PRIMEIRA coisa apos M5.begin() ou StickCP2.begin()
// Sem isso, o dispositivo DESLIGA ao soltar o botao power
pinMode(4, OUTPUT);
digitalWrite(4, HIGH);
```

**NOTA:** A biblioteca M5Unified/M5StickCPlus2 faz isso automaticamente no begin().
Se usar framework puro (sem M5 lib), DEVE fazer manualmente.

### Como Desligar

O firmware desliga deliberadamente setando GPIO4 = LOW:

```cpp
// Desligamento gracioso
digitalWrite(4, LOW);  // Corta a energia — dispositivo desliga
```

### Deep Sleep e GPIO4

**PROBLEMA:** Ao entrar em Deep Sleep, os GPIOs perdem estado. GPIO4 vai para LOW
e o dispositivo DESLIGA (corta energia) em vez de dormir.

**SOLUCAO:** Congelar GPIO4 antes do deep sleep:

```cpp
gpio_hold_en((gpio_num_t)4);  // Congela GPIO4 no estado atual (HIGH)
esp_deep_sleep_start();        // Agora dorme sem cortar energia
```

### Monitoramento de Bateria

- Tensao da bateria: GPIO38, via divisor resistivo 2:1
- Range: 3.0V (vazio) a 4.2V (cheio)
- Leitura: `analogRead(38)` → converter com formula

```cpp
float voltage = analogRead(38) * 2.0 * 3.3 / 4095.0;
int percent = map(voltage * 100, 300, 420, 0, 100);
```

### Brownout Detector

O ESP32 tem detector de subtensao que reseta o chip quando VDD cai abaixo de ~2.43V.

**Causas:**
- WiFi/BT ligando (pico de 500mA) com bateria fraca
- Muitos perifericos ativos simultaneamente
- Cabo USB ruim (resistencia alta)

**Sintomas:** `rst:0xc (SW_CPU_RESET)` + `Brownout detector was triggered`

**Mitigacao:**
- Sequenciar inicializacao (WiFi apos display)
- Nao desabilitar BOD (corrompe flash!)
- Manter bateria acima de 20%

---

## 3. Botoes

### Mapeamento

| Botao | GPIO | Tipo | Logica | Funcao Padrao |
|-------|------|------|--------|---------------|
| BtnA | 37 | Input-only | Invertida (LOW=pressionado) | Frontal — navegacao |
| BtnB | 39 | Input-only | Invertida (LOW=pressionado) | Lateral — navegacao |
| BtnC/BtnPWR | Logica especial | Power | NAO e GPIO — circuito de power | Ligar/desligar |

### Restricoes dos GPIO37 e GPIO39

- **Input-only:** Nao podem ser usados como output
- **Sem pull-up/down interno:** Tem pull-up EXTERNO no PCB
- **GPIO39 — Bug de hardware ESP32:** Pulsos fantasma quando ADC WiFi esta ativo

### Bug do GPIO39 (Chattering com WiFi)

**Errata ESP32 documentada:** GPIO36 e GPIO39 sofrem glitches quando o ADC do modulo WiFi
(SAR ADC2) esta ativo. Isso pode causar:
- Clicks fantasma no BtnB
- wasClicked() retornando true sem ninguem apertar
- Debounce da M5Unified rejeitando clicks reais

**Mitigacao:**
```cpp
#include <driver/adc.h>
adc_power_acquire();  // Mantem ADC ligado permanentemente — evita glitches
```

### M5Unified Button API

```cpp
M5.update();  // OBRIGATORIO — atualiza estado dos botoes

// Leitura de estado (persistem ate proximo M5.update):
M5.BtnA.isPressed()     // true enquanto pressionado
M5.BtnA.wasClicked()    // true se click completo (press+release)
M5.BtnA.wasHold()       // true se segurou por holdMs (default ~700ms)
M5.BtnA.pressedFor(ms)  // true se pressionado por >= ms milissegundos

// IMPORTANTE: wasClicked() le _currentState, NAO e destrutivo.
// Persiste ate o proximo M5.update(). Multiplas leituras no mesmo
// frame retornam o mesmo valor.
```

**REGRAS:**
- NUNCA usar StickCP2.BtnX — causa SIOF (Static Initialization Order Fiasco). Sempre M5.BtnX
- GPIO37/39 NAO tem pull-up/down PROGRAMAVEL — pull-up e fixo em hardware
- Botao C (power) acessivel via M5.BtnPWR.wasClicked() (NAO e GPIO comum)
- Deep sleep wakeup: so BtnA (GPIO37) e BtnB (GPIO39). BtnC NAO funciona como wakeup
- Cabo USB: preferir USB-A to USB-C (USB-C to USB-C pode falhar por falta de resistores)

---

## 4. Boot e Recovery

### Strapping Pins e Modos de Boot

| GPIO | Funcao no Boot | Valor para Boot Normal | Valor para Download |
|------|---------------|----------------------|-------------------|
| 0 | Modo de boot | HIGH (3.3V) | LOW (GND) |
| 2 | Modo de boot | Floating/LOW | Floating/LOW |
| 12 | Tensao flash | LOW (3.3V flash) | LOW |
| 15 | Log UART | HIGH (log ativo) | Qualquer |

### Conflito GPIO0 com Microfone

O GPIO0 e usado para:
1. **Strapping pin** — define modo de boot (HIGH=normal, LOW=download)
2. **Clock PDM do microfone SPM1423**

Se o microfone reter carga no GPIO0 durante reset, o ESP32 entra em download mode
e fica preso em "waiting for download" infinitamente.

### Procedimento de Recovery (Hard Reset)

```
1. Desconectar USB
2. Segurar Botao Power por 6-10 segundos (descarga completa)
3. Soltar, esperar 5 segundos
4. Reconectar USB
5. Pressionar Power 2 segundos para ligar
```

### Procedimento de Recovery (Download Mode Forcado)

Se o hard reset nao funcionar:

```
1. Desligar dispositivo, desconectar USB
2. Conectar fio entre G0 e GND no conector Grove
   (GPIO0 LOW = forca download mode)
3. Manter conexao
4. Conectar USB
5. Executar: esptool.py --chip esp32 --port COMX --baud 115200 erase_flash
6. Remover fio G0-GND
7. Gravar firmware novo
```

### Procedimento de Recovery (esptool durante crash loop)

```bash
# O crash loop reinicia a cada ~5s. O esptool tenta capturar
# a janela de bootloader entre reboots:
esptool.py --chip esp32 --port COMX --baud 115200 \
  --before default_reset --connect-attempts 20 erase_flash
```

### Procedimento de Recovery (ESP32 preso em reset)

Se o esptool abortou no meio do DTR/RTS toggle:

```bash
# O chip_id faz toggle DTR/RTS que desbloqueia o reset:
esptool.py --chip esp32 --port COMX --baud 115200 \
  --before default_reset chip_id
# Se responder, gravar firmware normalmente
```

---

## 5. Comunicacao

### I2C Interno (GPIO21/22)

| Dispositivo | Endereco | Funcao |
|-------------|----------|--------|
| MPU6886 | 0x68 | Acelerometro + Giroscopio |
| BM8563 | 0x51 | Relogio Tempo Real |

**I2C Bus Hang:** Se uma transacao I2C for interrompida (crash, timeout), o sensor
pode manter SDA em LOW indefinidamente, travando TODO o barramento.

**Solucao:** Bit-banging de 9 pulsos de clock no SCL para liberar o barramento.
A M5Unified faz isso automaticamente na inicializacao.

### I2C Externo (GPIO32/33 via Grove)

- Expansao para sensores externos
- Tolerancia: 3.3V APENAS (5V danifica o ESP32!)
- Multiplos dispositivos: precisam ter enderecos I2C diferentes

### SPI (Display)

| Pino | GPIO | Funcao |
|------|------|--------|
| MOSI | 15 | Dados |
| SCK | 13 | Clock |
| CS | 5 | Chip Select |
| DC | 14 | Data/Command |
| RST | 12 | Reset |
| BLK | 27 | Backlight PWM |

Clock SPI: 20-40 MHz (ST7789V suporta ate ~15 MHz teorico, mas funciona acima).

### USB Serial (CH9102)

| Pino | GPIO | Funcao |
|------|------|--------|
| TX | 1 | ESP32 → PC |
| RX | 3 | PC → ESP32 |
| DTR | — | Controla GPIO0 (boot mode) via transistor |
| RTS | — | Controla EN (reset) via transistor |

**Download mode via DTR/RTS:**
1. DTR LOW → GPIO0 LOW (download mode)
2. RTS HIGH→LOW → EN toggle (reset)
3. ESP32 reseta com GPIO0 LOW → entra em download mode
4. esptool envia firmware

**PROBLEMA:** Se o toggle DTR/RTS for interrompido (upload abortado), o ESP32 pode
ficar com EN preso em LOW (reset permanente). Solucao: `esptool chip_id` faz novo toggle.

---

## 6. Programacao

### M5Unified vs Libs Legadas

| Aspecto | M5StickCPlus.h (legado) | M5Unified.h (atual) |
|---------|------------------------|---------------------|
| PMIC | M5.Axp (AXP192) | M5.Power (generico) |
| Bateria | M5.Axp.GetBatVoltage() | M5.Power.getBatteryLevel() |
| Board | Hardcoded para Plus v1 | Auto-detecta via I2C probing |
| Botoes | StickCP2.BtnA | M5.BtnA |
| Display | Libs especificas | M5GFX (unificado) |

**REGRA:** Usar SEMPRE M5Unified (#include <M5Unified.h> ou #include <M5StickCPlus2.h>
que inclui M5Unified internamente).

### PlatformIO — Configuracao Obrigatoria

```ini
[env:sensor_a]
platform = espressif32@6.12.0
board = m5stick-c-plus2
framework = arduino
lib_deps = m5stack/M5StickCPlus2@^1.0.2

; OBRIGATORIO para 8MB flash + PSRAM
board_build.partitions = default_8MB.csv
board_upload.maximum_size = 8388608
build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue   ; Fix errata PSRAM cache
```

### Watchdog Timer

O ESP32 tem Task Watchdog Timer (TWDT) com timeout default ~5s no Arduino.
Se o loop() bloquear por mais de 5s, o watchdog reseta o ESP32.

**Causas de watchdog reset:**
- delay() longo (>5s)
- while() infinito
- I2C bus hang
- SPIFFS/NVS operacao longa

**Prevencao:**
- Nunca usar delay() > 1s
- Sempre ter timeout em loops
- Usar yield() ou vTaskDelay() em operacoes longas

---

## 9. BM8563 (RTC)

### Visao Geral

- Relogio de tempo real CMOS, baixo consumo
- Cristal: 32.768 kHz (interno)
- Tensao: 1.8V - 5.5V
- Consumo sleep: 250 nA
- I2C: endereco 0x51, ate 400 kHz

### Mapa de Registros

| Endereco | Nome | Conteudo |
|----------|------|----------|
| 0x00 | Control/Status 1 | TEST1, STOP, TESTC |
| 0x01 | Control/Status 2 | TI/TP, AF, TF, AIE, TIE |
| 0x02 | Segundos | BCD 00-59 + bit VL (bit7 = power-down flag) |
| 0x03 | Minutos | BCD 00-59 |
| 0x04 | Horas | BCD 00-23 |
| 0x05 | Dia | BCD 01-31 |
| 0x06 | Dia da semana | 0-6 (0=domingo) |
| 0x07 | Mes/Seculo | BCD 01-12 + bit C (seculo) |
| 0x08 | Ano | BCD 00-99 |
| 0x09 | Alarme minuto | AE bit + BCD |
| 0x0A | Alarme hora | AE bit + BCD |
| 0x0B | Alarme dia | AE bit + BCD |
| 0x0C | Alarme dia semana | AE bit + valor |
| 0x0D | CLKOUT | FE, FD1:FD0 |
| 0x0E | Timer controle | TE, TD1:TD0 |
| 0x0F | Timer contagem | Valor 8-bit countdown |

### Leitura da Hora

```cpp
Wire.beginTransmission(0x51);
Wire.write(0x02);           // Registro de segundos
Wire.endTransmission();
Wire.requestFrom(0x51, 7);  // 7 bytes (seg ate ano)

uint8_t sec  = bcd2dec(Wire.read() & 0x7F);
uint8_t min  = bcd2dec(Wire.read() & 0x7F);
uint8_t hour = bcd2dec(Wire.read() & 0x3F);
uint8_t day  = bcd2dec(Wire.read() & 0x3F);
uint8_t wday = Wire.read() & 0x07;
uint8_t mon  = bcd2dec(Wire.read() & 0x1F);
uint8_t year = bcd2dec(Wire.read());
```

---

## 10. SPM1423 (Microfone)

### Especificacoes

| Parametro | Valor |
|-----------|-------|
| Tipo | MEMS digital, saida PDM |
| Tensao | 1.6 - 3.6V (tipico 1.8V) |
| Corrente | ~600 uA |
| Sensibilidade | -26 dBFS |
| SNR | 62 dBA |
| Faixa de frequencia | 100 - 10000 Hz |
| Pinos no M5StickC Plus2 | CLK=GPIO0, DATA=GPIO34 |

### CONFLITO CRITICO: GPIO0

O CLK do microfone esta no GPIO0 — que e strapping pin de boot!
Se o microfone reter carga no GPIO0 durante reset, o ESP32 entra em download mode.

### Captura de Audio

```cpp
i2s_config_t cfg = {
    .mode = I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM,
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
};
i2s_pin_config_t pins = {
    .ws_io_num = GPIO_NUM_0,     // PDM CLK
    .data_in_num = GPIO_NUM_34,  // PDM DATA
};
```

---

## 11. ST7789V (Display LCD)

### Especificacoes no M5StickC Plus2

| Parametro | Valor |
|-----------|-------|
| Resolucao | 135 x 240 pixels |
| Controller | ST7789V2 |
| Interface | SPI 4-line |
| Cores | 65K (RGB565, 16-bit) |
| Frame rate | 60 Hz (default) |

### Pinos SPI

| Funcao | GPIO |
|--------|------|
| MOSI | 15 |
| SCK | 13 |
| CS | 5 |
| DC | 14 |
| RST | 12 (strapping pin!) |
| Backlight | 27 (PWM) |

### Comandos Principais

| Comando | Hex | Funcao |
|---------|-----|--------|
| SWRESET | 0x01 | Software reset (esperar 5ms) |
| SLPOUT | 0x11 | Sair do sleep (esperar 120ms) |
| INVON | 0x21 | Inversao de cores (necessario no ST7789V) |
| DISPON | 0x29 | Ligar display |
| CASET | 0x2A | Definir colunas (area de escrita) |
| RASET | 0x2B | Definir linhas (area de escrita) |
| RAMWR | 0x2C | Escrever pixels |
| MADCTL | 0x36 | Orientacao (rotacao, mirror) |
| COLMOD | 0x3A | Formato de cor (0x55 = RGB565) |

### Controle de Backlight

```cpp
ledcSetup(0, 44100, 8);      // Canal 0, 44.1kHz, 8-bit
ledcAttachPin(27, 0);         // GPIO27
ledcWrite(0, brightness);     // 0=off, 255=max
```

---

## 12. Problemas Conhecidos e Solucoes

### Problema: Dispositivo "nao liga"

**Causa:** Firmware nao seta GPIO4 HIGH (Power Latch).
**Solucao:** Usar M5Unified (faz automaticamente) ou setar manualmente.

### Problema: Preso em "waiting for download"

**Causa:** GPIO0 LOW por capacitancia do microfone.
**Solucao:** Hard reset (power 6-10s) ou bypass G0-GND via Grove.

### Problema: Brownout boot loop

**Causa:** Bateria fraca + WiFi/BT ligando.
**Solucao:** Carregar, sequenciar init, nao desabilitar BOD.

### Problema: I2C Bus Hang

**Causa:** Transacao interrompida, sensor segura SDA LOW.
**Solucao:** M5Unified faz recovery automatico. Em caso manual: 9 pulsos SCL.

### Problema: Botoes nao respondem (GPIO39 chattering)

**Causa:** Errata ESP32 — ADC WiFi causa glitches no GPIO36/39.
**Solucao:** `adc_power_acquire()` no setup().

### Problema: ESP32 preso em reset apos upload abortado

**Causa:** DTR/RTS toggle interrompido, EN preso em LOW.
**Solucao:** `esptool chip_id` faz novo toggle e desbloqueia.

### Problema: PSRAM cache corruption

**Causa:** Errata ESP32 — cache lines corrompidas em dual-core com PSRAM.
**Solucao:** Flag `-mfix-esp32-psram-cache-issue` no build_flags.

### Problema: Flash 4MB em vez de 8MB

**Causa:** Particao default assume 4MB.
**Solucao:** `board_build.partitions = default_8MB.csv` no platformio.ini.

### Problema: Crash (Guru Meditation Error) ao usar StickCP2.BtnX

**Causa:** SIOF (Static Initialization Order Fiasco) — StickCP2.BtnX acessa
objeto nao inicializado quando M5Unified e o framework principal.
**Solucao:** SEMPRE usar M5.BtnX, NUNCA StickCP2.BtnX.

---

> Secoes 7 (ESP32-PICO detalhado) e 8 (MPU-6886 detalhado) serao adicionadas
> quando os agentes de analise completarem a extracao dos datasheets.

---

*Documento compilado para o projeto GESTUUM — Science Fair COREE 2026*
*Fontes: esp32-pico_series_datasheet_en.pdf, MPU-6886-000193+v1.1_GHIC_en.pdf,
BM8563_V1.1_cn.pdf, SPM1423HM4H-B_datasheet_en.pdf, ST7789V.pdf,
M5StickC PLUS2 Documentacao e Programacao.pdf, program.pdf (1-3)*
# M5StickC PLUS2 -- Referencia Tecnica Completa

> Compilado em 2026-04-03 a partir da documentacao oficial M5Stack, datasheets Espressif,
> forums da comunidade e relatorios de campo. Voltado para desenvolvimento de firmware
> em C++ (Arduino/PlatformIO) com foco no projeto GESTUUM.

---

## 1. HARDWARE

### 1.1 Comparacao: M5StickC Plus vs M5StickC PLUS2

| Caracteristica | M5StickC Plus (Gen 1) | M5StickC PLUS2 (Gen 2) | Impacto no Desenvolvimento |
|---|---|---|---|
| **SoC** | ESP32-PICO-D4 | ESP32-PICO-V3-02 | Revisao de silicio atualizada, correcoes de seguranca |
| **Flash** | 4 MB | **8 MB** | Mais espaco para SPIFFS/LittleFS e OTA |
| **PSRAM** | Ausente | **2 MB** | Permite buffers graficos pesados, mas exige mitigacao de bug de cache |
| **Gerenciamento Energia** | PMIC dedicado AXP192 | Controle logico direto (Timer Power via GPIO4) | Codigo antigo com `M5.Axp` causa falha fatal de compilacao. Requer gestao via GPIO4 |
| **Bateria** | 120 mAh | **200 mAh** | Maior autonomia, melhor tolerancia a picos de corrente do radio Wi-Fi |
| **Conversor USB-Serial** | CH552 | **CH9102** | Exige instalacao de driver CH9102_VCP_SER especifico |
| **Acionamento de Energia** | Botao C (2 segundos) | Botao C + Retencao de pino HOLD (GPIO4) | Firmware que nao seta GPIO4 HIGH desliga o dispositivo instantaneamente |

**Mudanca mais critica:** A remocao do AXP192. Antes, o PMIC atuava como mestre independente
que negociava voltagem da bateria, carga USB e acionamento do display. No PLUS2, essas
responsabilidades foram transferidas inteiramente para o firmware rodando no ESP32, combinadas
a uma topologia de circuito de retencao passiva. Essa dependencia de software para manter o
proprio hardware energizado e a raiz da maioria dos problemas de "morte subita".

### 1.2 Componentes Internos

| Componente | Modelo/Tipo | Funcao |
|---|---|---|
| SoC | ESP32-PICO-V3-02 | Dual-core Xtensa LX6 @ 240 MHz, Wi-Fi + BT |
| Flash | 8 MB (integrada no SiP) | Armazenamento de firmware, SPIFFS, OTA |
| PSRAM | 2 MB (integrada no SiP) | RAM externa para buffers grandes |
| Display | TFT 1.14" ST7789V2 | 135x240 pixels, colorido, interface SPI |
| IMU | MPU6886 | Acelerometro + Giroscopio 6 eixos (I2C interno) |
| RTC | BM8563 | Relogio de tempo real, pode gerar IRQ para wakeup (I2C interno) |
| Microfone | SPM1423 | Microfone PDM (CLK=GPIO0, DATA=GPIO34) |
| Buzzer | Passivo | Requer injecao de frequencia PWM (GPIO2) |
| Emissor IR | LED IR | Transmissao de pulsos infravermelhos (GPIO19) |
| LED Vermelho | Indicador | Compartilhado com emissor IR (GPIO19), logica invertida |
| LED Verde | Indicador HW | Atrelado a carga da bateria/desligamento, NAO programavel |
| USB-Serial | CH9102 | Ponte USB-UART para programacao e debug |
| Bateria | LiPo 200 mAh | Alimentacao portatil |
| Conector | Grove HY2.0-4P | Expansao externa (GND, 5V, GPIO32, GPIO33) |

### 1.3 Pinout Completo

#### Display SPI (ST7789V2)

| Funcao | GPIO | Direcao | Observacoes |
|---|---|---|---|
| MOSI (Master Out Slave In) | **GPIO 15** | Output | Transmissao de dados de cor e comandos. **Strapping pin** |
| CLK (Clock) | **GPIO 13** | Output | Sincronizacao do barramento SPI |
| DC (Data/Command) | **GPIO 14** | Output | Alterna entre dados brutos e instrucoes de controle |
| RST (Reset) | **GPIO 12** | Output | Reset por hardware do ST7789V2. **Strapping pin** (tensao da flash) |
| CS (Chip Select) | **GPIO 5** | Output | Habilita comunicacao com o display. **Strapping pin** |
| BL (Backlight) | **GPIO 27** | Output | Controle de brilho via PWM |

#### Sensores Internos (I2C Interno)

| Componente | SDA | SCL | Endereco I2C | Observacoes |
|---|---|---|---|---|
| IMU MPU6886 | GPIO 21 | GPIO 22 | 0x68 | Acelerometro + Giroscopio. Leitura continua |
| RTC BM8563 | GPIO 21 | GPIO 22 | 0x51 | Cronometragem independente. Pode gerar IRQ para wakeup |

> **ATENCAO:** Dois escravos I2C no mesmo barramento (GPIO21/22). Falhas de leitura no
> MPU6886 podem travar o barramento I2C inteiro ("I2C Bus Hang"), paralisando tambem
> o acesso ao RTC.

#### Audio, Botoes, LEDs e Indicadores

| Componente | GPIO | Tipo | Detalhamento |
|---|---|---|---|
| Microfone SPM1423 CLK | **GPIO 0** | Output | **CRITICO: Strapping pin + Boot mode.** Compartilhamento com mic causa conflito de boot |
| Microfone SPM1423 DATA | GPIO 34 | Input-only | Dados PDM do microfone |
| Botao A (Frontal) | **GPIO 37** | Input-only | Logica invertida (LOW quando pressionado), pull-up interno |
| Botao B (Lateral) | **GPIO 39** | Input-only | Logica invertida (LOW quando pressionado), pull-up interno |
| Botao C (Energia/Reset) | Logica especial | -- | Aciona hardware primario. Monitorado pelo sistema unificado |
| Buzzer (Passivo) | **GPIO 2** | Output | Requer PWM para gerar tons. **Strapping pin** |
| Emissor IR | GPIO 19 | Output | Compartilhado com LED vermelho |
| LED Vermelho | GPIO 19 | Output | Compartilhado com IR. Logica invertida (LOW liga o LED) |
| LED Verde | N/A | HW-only | Indicador de carga/desligamento. NAO programavel |

#### Power Management

| Funcao | GPIO | Observacoes |
|---|---|---|
| **HOLD (Power Latch)** | **GPIO 4** | **CRITICO.** Deve ser HIGH para manter o dispositivo ligado |
| **Bateria (ADC)** | **GPIO 38** | Leitura de tensao via divisor resistivo 2:1 |

#### Porta Grove (Expansao Externa)

| Pino | GPIO | Funcao Padrao | Limites |
|---|---|---|---|
| GND | -- | Terra | -- |
| 5V | -- | Alimentacao | Vem do boost da bateria ou USB |
| SDA | **GPIO 32** | I2C externo (dados) | **Maximo 3.3V nos pinos logicos!** Sem level shifter, 5V causa dano fisico |
| SCL | **GPIO 33** | I2C externo (clock) | **Maximo 3.3V nos pinos logicos!** |

> **PERIGO:** Embora a alimentacao seja 5V, os pinos logicos GPIO32 e GPIO33 toleram
> estritamente 3.3V. Conectar sensores 5V TTL sem level shifter causa dano irreversivel ao SoC.

### 1.4 Conectores

**Grove (HY2.0-4P):**
- Localizado na base do chassi
- 4 condutores: GND, 5V, GPIO32, GPIO33
- Funcao padrao: I2C externo (Porta A nas interfaces visuais)
- Suporta multiplos modulos I2C via hub passivo Y, DESDE QUE todos tenham enderecos distintos
- NAO misturar I2C com analogico/digital/serial no mesmo hub -- corrompe todo o barramento

**HAT:**
- Conector de 8 pinos no topo
- Compartilha pinos com funcoes internas -- verificar conflitos antes de usar

---

## 2. POWER MANAGEMENT (CRITICO)

### 2.1 Power Latch -- Como Funciona (GPIO4 HOLD)

O M5StickC PLUS2 usa um sistema de **retencao de energia passiva** (Power Latch) em vez
de um PMIC dedicado.

**Fluxo de energizacao:**

```
1. Usuario pressiona Botao C
2. Energia flui momentaneamente para o ESP32
3. CPU inicia ciclo de clock
4. PRIMEIRA COISA que o firmware deve fazer:
   -> Setar GPIO4 = HIGH
5. GPIO4 HIGH fecha transistor no circuito de energia
6. Bateria fica conectada aos reguladores INDEPENDENTE do botao
7. Dispositivo permanece ligado
```

**Fluxo de desligamento:**

```
1. Firmware seta GPIO4 = LOW
2. Transistor de energia abre
3. Bateria desconecta dos reguladores
4. Dispositivo desliga ("suicidio eletrico gracioso")
```

**Codigo obrigatorio no setup():**

```cpp
// M5Unified faz isso automaticamente:
auto cfg = M5.config();
M5.begin(cfg);
// Internamente executa: gpio_set_level(GPIO_NUM_4, 1);

// Se usar codigo manual SEM M5Unified:
pinMode(4, OUTPUT);
digitalWrite(4, HIGH);  // CRITICO: sem isso o dispositivo desliga
```

### 2.2 Por Que o Dispositivo "Nao Liga" -- Causa e Solucao

**Sintoma:** LED verde acende enquanto segura o Botao C, tela pisca levemente, sistema
desliga quando solta o dedo. Parece "bricked".

**Causa real:** O firmware carregado nao executa `GPIO4 = HIGH`. Pode ser:
- Sketch Arduino rudimentar que ignora a placa hospedeira
- Codigo obsoleto desenhado para a geracao anterior (Plus, nao Plus2)
- Firmware compilado incorretamente
- Firmware "ofensivo" (Bruce, Nemo, Marauder) que corrompeu particoes

**NAO e** bootloader destruido. E falha de orquestracao de energia em nivel de software.

**Solucao imediata:**
1. Segurar Botao C por 6-10 segundos (Hard Reset / descarga de capacitores)
2. Soltar e aguardar alguns segundos
3. Conectar USB e regravar firmware correto via M5Burner
4. Se nao funcionar: bridge G0-GND para forcar download mode (ver secao 4)

### 2.3 Deep Sleep e o Problema do GPIO4

**O problema:** Ao invocar `esp_deep_sleep_start()`, a matriz de pinos GPIO perde tensao.
GPIO4 cai para LOW. Isso NAO coloca a placa em suspensao -- **corta a alimentacao primaria
de todo o sistema** irreversivelmente.

Apos esse corte, a placa NAO pode despertar por timers internos do ESP32 porque a CPU
perdeu contato eletrico com a bateria.

**Solucao obrigatoria -- congelar GPIO4 antes de dormir:**

```cpp
// C++ / Arduino
gpio_hold_en((gpio_num_t)4);      // Congela estado do pino no dominio RTC
gpio_deep_sleep_hold_en();         // Mantem hold durante deep sleep
esp_deep_sleep_start();            // Agora sim, pode dormir com seguranca
```

```python
# MicroPython
from machine import Pin
p4 = Pin(4, Pin.OUT, value=1, hold=True)  # hold=True congela o estado
# OU
p4 = Pin(4, Pin.OUT, value=1)
p4.init(pull=Pin.PULL_HOLD)
```

**Fontes de wakeup disponiveis apos Deep Sleep:**

| Metodo | Descricao | Observacoes |
|---|---|---|
| **RTC Alarm (Ext0)** | BM8563 emite sinal no pino INT | Cronometragem independente do ESP32 |
| **Botao A (Ext1)** | GPIO37 -- detecta transicao para LOW | Pull-up interno; configurar wake mask para ALL_LOW |
| **Botao B (Ext1)** | GPIO39 -- detecta transicao para LOW | Pull-up interno; configurar wake mask para ALL_LOW |
| **Botao C** | NAO funciona trivialmente como wakeup | Arquitetura disjunta do sistema de energia |

### 2.4 Bateria: Monitoramento via GPIO38

A voltagem da bateria LiPo (faixa tipica: 3.0V a 4.2V) passa por um **divisor de tensao
resistivo 2:1** e e canalizada para GPIO38.

**Leitura:**
- ADC interno do ESP32 quantifica o nivel de tensao
- **NAO sondar GPIO38 diretamente** -- leituras ruidosas devido a tolerancia dos resistores
- Usar as interfaces abstraidas da M5Unified:

```cpp
// Com M5Unified (recomendado)
int voltage = M5.Power.getBatteryVoltage();  // Retorna em mV
int level = M5.Power.getBatteryLevel();      // Retorna 0-100%
```

### 2.5 Brownout Detector (BOD): Causas e Mitigacao

**O que e:** Sentinela de hardware que monitora VDD da CPU. Quando a tensao cai abaixo
de ~2.43-2.7V (dependendo da revisao de silicio), forca reset para preservar integridade
das logicas em silicio.

**Sintoma no serial monitor:**
```
Brownout detector was triggered
rst:0xc (SW_CPU_RESET)
```

**Causas principais:**
1. **Picos de corrente do radio Wi-Fi/BT** -- pode cruzar 500 mA
2. **Bateria LiPo em fase de esgotamento** -- nao consegue suprir picos
3. **LCD em brilho maximo + Wi-Fi simultaneo** -- excede capacidade da bateria
4. **Cabos USB de baixa qualidade** -- resistencia excessiva causa queda de tensao

**Brownout Boot Loop:** O reset causado pelo BOD reinicia o ESP32, que tenta ligar o radio
novamente na mesma bateria exausta, causando outro brownout -- loop infinito.

**Mitigacao (DO e DONT):**

| FAZER | NAO FAZER |
|---|---|
| Sequenciar inicializacao: delay entre subsistemas | Ligar Wi-Fi + LCD brilho max ao mesmo tempo |
| Esperar estabilizacao capacitiva antes de `WiFi.begin()` | Desabilitar BOD via `WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0)` |
| Reduzir brilho do LCD antes de ligar radio | Ignorar alertas de brownout no log |
| Usar cabo USB de qualidade (curto, gauge grosso) | Usar cabos USB longos/finos |
| Capacitor MLCC ceramico no barramento 5V (soldagem) | Operar com bateria < 10% + radio ligado |

> **NUNCA desabilitar o BOD.** Flash alimentada com tensao insuficiente causa gravacao
> inconsistente, corrompendo fatalmente particoes NVS de calibracao e tornando o
> dispositivo nao-responsivo a futuras inicializacoes.

**Sequenciamento recomendado no setup():**

```cpp
void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);              // Seta GPIO4 HIGH + inicia display

    // Delay para estabilizacao capacitiva
    delay(100);

    // Liga Wi-Fi DEPOIS da estabilizacao
    WiFi.begin(ssid, password);

    // Ajusta brilho DEPOIS do Wi-Fi estabilizar
    delay(500);
    M5.Display.setBrightness(128);  // Nao usar 255 com bateria
}
```

---

## 3. BOTOES

### 3.1 Mapeamento

| Botao | GPIO | Tipo | Logica | Uso Tipico |
|---|---|---|---|---|
| **BtnA** (Frontal) | GPIO 37 | Input-only | Invertida (LOW = pressionado) | Acao principal, confirmacao |
| **BtnB** (Lateral) | GPIO 39 | Input-only | Invertida (LOW = pressionado) | Navegacao, cancelamento |
| **BtnC** (Energia) | Logica especial | -- | -- | Liga/desliga, reset de hardware |

### 3.2 Pull-up e Logica Invertida

Ambos BtnA e BtnB possuem resistores de pull-up internos que forcam estado HIGH em repouso.
Quando pressionado, o botao conecta o pino a GND, causando transicao HIGH -> LOW.

```cpp
// Com M5Unified (recomendado)
M5.update();  // Atualiza estado dos botoes
if (M5.BtnA.wasPressed()) {
    // Botao A foi pressionado
}
if (M5.BtnB.wasHold()) {
    // Botao B foi segurado
}

// Leitura manual (NAO recomendado por causa do chattering)
pinMode(37, INPUT);  // BtnA -- NAO usar INPUT_PULLUP, ja tem pull-up HW
int state = digitalRead(37);  // 0 = pressionado, 1 = solto
```

### 3.3 Restricoes dos GPIOs 37 e 39

| Restricao | Detalhes |
|---|---|
| **Input-only** | GPIO 34-39 sao estritamente input-only no ESP32. NAO podem ser usados como output |
| **Sem pull-up/pull-down interno programavel** | O pull-up e fisico (resistor no PCB), nao via software |
| **ADC chattering com Wi-Fi** | Quando o radio Wi-Fi esta ativo, o ADC2 sofre interferencia. GPIO37 e GPIO39 estao no ADC1, mas leituras analogicas adjacentes podem ser afetadas |
| **Sem wakeup Ext0** | GPIO37 e GPIO39 so suportam wakeup via Ext1 (mascara de bitmask), nao Ext0 (pino unico) |

---

## 4. BOOT E RECOVERY

### 4.1 Strapping Pins do ESP32

Durante os microssegundos imediatos apos Power-on Reset, a ROM do ESP32 faz varredura
do nivel de tensao nos Strapping Pins para configurar parametros criticos:

| Strapping Pin | GPIO | Funcao no Boot | Uso no PLUS2 | Conflito |
|---|---|---|---|---|
| **GPIO 0** | 0 | **Boot Mode:** HIGH=Flash (app normal), LOW=Download (UART) | CLK do Microfone SPM1423 | **CRITICO** -- capacitancia do mic pode ancorar GPIO0 em LOW |
| **GPIO 2** | 2 | Deve ser LOW ou flutuante para download mode | Buzzer passivo | Menor risco se buzzer nao acionado no boot |
| **GPIO 12** | 12 | Seleciona tensao da flash (LOW=3.3V, HIGH=1.8V) | Reset do display ST7789V2 | Blindado como saida interna; nao alterar externamente |
| **GPIO 15** | 15 | Habilita/desabilita mensagens de debug UART | MOSI do display SPI | Blindado como saida interna |
| **GPIO 5** | 5 | -- | CS do display SPI | Strapping pin secundario |

### 4.2 Conflito GPIO0 com Microfone SPM1423

**O problema mais frustrante do PLUS2.**

O GPIO0 e simultaneamente:
1. **Strapping pin** que determina o modo de boot (HIGH=app, LOW=download)
2. **Clock do microfone PDM** SPM1423

O microfone MEMS possui capacitancia passiva e pode reter energia residual apos soft resets.
Essa anomalia atua como resistor de pull-down parasita, ancorando GPIO0 em LOW.

**Resultado:** A CPU "acredita" que o usuario pressionou um botao imaginario de Boot,
prendendo o dispositivo em "waiting for download" mode.

**No serial monitor:**
```
rst:0x1 (POWERON_RESET)
boot:0x3 (DOWNLOAD_BOOT(UART0/UART1/SDIO_REI_REO_V2))
waiting for download
```

**No IDE:**
```
A fatal error occurred: failed to connect to esp32: wrong boot mode detected (0xb)!
```

### 4.3 Como Entrar em Download Mode (GPIO0 LOW)

**Quando voce QUER entrar em download mode** (para regravar firmware):

1. Conectar fio DuPont ou clip metalico entre **G0** e **GND**
2. ENQUANTO mantem a ponte, conectar cabo USB
3. O dispositivo entra em download mode
4. Gravar firmware via M5Burner ou esptool
5. Remover a ponte G0-GND
6. Reiniciar o dispositivo

### 4.4 Procedimento de Recovery Completo

**Para dispositivo que parece "bricked" (nao liga, tela preta, loop de boot):**

**Passo 1 -- Hard Reset (descarga de capacitores):**
```
1. Segurar Botao C por 6-10 segundos ININTERRUPTOS
2. Ignorar qualquer atividade de LEDs durante o processo
3. Soltar o botao
4. Aguardar 3-5 segundos para dissipacao completa dos capacitores
```

**Passo 2 -- Instalar driver CH9102:**
- Windows: CH9102_VCP_SER_Windows
- macOS: CH9102_VCP_SER_MacOS v1.7
- Se a porta flutuar: trocar cabo USB-C por cabo USB-A para USB-C
  (elimina problemas de negociacao PD em conexoes C-to-C)

**Passo 3 -- Forcar Download Mode (se Passo 1 nao resolver):**
```
1. Conectar fio entre G0 e GND (ponte fisica)
2. Com a ponte ativa, conectar cabo USB ao PC
3. O dispositivo entra em download mode forcado
4. Isso sobrepoe a capacitancia parasita do SPM1423
```

**Passo 4 -- Erase completo via M5Burner:**
```
1. Abrir M5Burner
2. Selecionar firmware "M5StickCPlus2 UserDemo" ou "Factory Test"
3. ANTES de Burn: clicar em ERASE
   -> Isso comanda esptool a preencher TODA a flash com 0x00
   -> Erradica NVS sujos, particoes fantasmas, configs RF corrompidas
4. Apos erase completo: clicar em Burn
5. Baud rate recomendado: 1500000 bps
6. Remover ponte G0-GND
7. Reiniciar dispositivo
```

**Passo 5 -- Verificacao:**
```
1. Desconectar USB
2. Pressionar Botao C -- dispositivo deve ligar normalmente
3. Verificar que display mostra o demo/factory test
```

> **Nota sobre firmwares ofensivos (Bruce, Nemo, Marauder):** Estas distribuicoes podem
> desconfigurar particoes do ESP, reformatar EEPROM, bloquear transacoes e causar loops
> de boot nao-mapeados. O ERASE completo via M5Burner e a unica solucao confiavel.
> Regravar via Arduino IDE ou PlatformIO nao e suficiente porque so reescreve a particao
> App0, sem limpar particoes adjacentes corrompidas.

---

## 5. COMUNICACAO

### 5.1 I2C: Barramento Interno (GPIO21/22)

| Parametro | Valor |
|---|---|
| SDA | GPIO 21 |
| SCL | GPIO 22 |
| Escravos | MPU6886 (0x68), BM8563 (0x51) |
| Tipo | Open-Drain com pull-up |
| Acesso | Somente componentes internos (soldados no PCB) |

### 5.2 I2C: Barramento Externo (GPIO32/33 Grove)

| Parametro | Valor |
|---|---|
| SDA | GPIO 32 |
| SCL | GPIO 33 |
| Conector | Grove HY2.0-4P |
| Tensao logica | **3.3V maximo** (alimentacao 5V, logica 3.3V) |
| Tipo | Open-Drain com pull-up |

**Regras para expansao via Grove:**
- Todos os modulos devem operar no padrao I2C
- Todos devem ter enderecos hexadecimais distintos
- NAO misturar I2C com analogico/digital/serial no mesmo hub
- NAO conectar sensores 5V TTL sem level shifter

### 5.3 I2C Bus Hang: Causa e Solucao

**Causa:** Um escravo I2C (tipicamente MPU6886) trava durante transacao de dados. O escravo
segura a linha SDA em LOW indefinidamente, bloqueando TODA comunicacao I2C no barramento.

**Mecanismo detalhado:**
1. Mestre (ESP32) inicia transacao I2C
2. Interferencia (flutuacao, acoplamento do Wi-Fi, firmware quebrado) interrompe SCL
3. Mestre abandona transacao sem enviar condicao de STOP
4. Escravo fica travado no meio da transmissao, segurando SDA em LOW
5. Nenhuma comunicacao I2C e possivel enquanto SDA estiver LOW

**Sintomas:**
- Sensores retornam valores nulos
- Serial monitor: `Error writing to I2C bus` ou `error 5` em I2C scanner
- Display continua funcionando (SPI, nao I2C), mas IMU e RTC param

**Solucao -- Bit-Banging de recovery (9 clock pulses):**

```cpp
// Enviar 9 pulsos de clock manualmente para destravar o escravo
void i2c_recovery(int sda_pin, int scl_pin) {
    pinMode(sda_pin, INPUT_PULLUP);
    pinMode(scl_pin, OUTPUT);

    // 9 pulsos de clock para forcar escravo a liberar SDA
    for (int i = 0; i < 9; i++) {
        digitalWrite(scl_pin, LOW);
        delayMicroseconds(5);
        digitalWrite(scl_pin, HIGH);
        delayMicroseconds(5);
    }

    // Enviar condicao de STOP
    pinMode(sda_pin, OUTPUT);
    digitalWrite(sda_pin, LOW);
    delayMicroseconds(5);
    digitalWrite(scl_pin, HIGH);
    delayMicroseconds(5);
    digitalWrite(sda_pin, HIGH);  // STOP = SDA HIGH enquanto SCL HIGH

    // Reiniciar I2C normalmente
    Wire.begin(sda_pin, scl_pin);
}
```

**Prevencao:**
- NUNCA usar `while` loops sem timeout ao ler sensores I2C
- M5Unified (via M5GFX) ja inclui mecanismos de `i2c_context[port].unlock()`
- Implementar timeouts estritos em todas as leituras I2C

### 5.4 SPI: Display ST7789V2

| Sinal | GPIO | Funcao |
|---|---|---|
| MOSI | GPIO 15 | Dados de cor e comandos |
| CLK | GPIO 13 | Clock do barramento SPI |
| DC | GPIO 14 | Alterna entre dados e instrucoes |
| RST | GPIO 12 | Reset por hardware do painel |
| CS | GPIO 5 | Chip Select |
| BL | GPIO 27 | Backlight (PWM) |

**Resolucao:** 135x240 pixels
**Driver:** ST7789V2
**Velocidade:** SPI de alta velocidade (tipico 40 MHz)

### 5.5 USB Serial: CH9102

| Parametro | Valor |
|---|---|
| Chip | CH9102 |
| Driver | CH9102_VCP_SER (Windows/macOS) |
| Sinais | DTR + RTS (usados para auto-reset e download mode) |
| Baud rate para flash | 1500000 bps (recomendado) |

**Problemas comuns:**
- **Timeout no upload:** Reinstalar driver CH9102, trocar cabo USB
- **"Failed to write to target RAM":** Reinstalar driver
- **Porta nao aparece:** Cabo USB-C para USB-C pode falhar por falta de resistores de
  terminacao. Usar cabo USB-A para USB-C
- **DTR/RTS para download mode:** O circuito CH9102 usa DTR e RTS para sequenciar
  GPIO0 e EN, forcando download mode automaticamente durante o upload

---

## 6. PROGRAMACAO

### 6.1 M5Unified vs Libs Legadas

| Biblioteca | Status | Include | Observacoes |
|---|---|---|---|
| **M5Unified** | **ATUAL -- USAR ESTA** | `#include "M5Unified.h"` | Auto-detecta modelo da placa. Suporta Plus2 nativamente |
| M5StickCPlus2 | Especifica | `#include <M5StickCPlus2.h>` | Funciona mas sem flexibilidade multi-modelo |
| M5StickCPlus | **OBSOLETA** | `#include <M5StickCPlus.h>` | NAO funciona no Plus2. Referencia AXP192 inexistente |
| M5StickC | **OBSOLETA** | `#include <M5StickC.h>` | Causa falha fatal de compilacao no Plus2 |

**M5Unified auto-detecta:**
- Modelo fisico da placa (interroga barramento interno no boot)
- Ponteiros de forca, display, bateria, botoes
- Mapeamento energetico (substitui `M5.Axp` por `M5.Power`)

```cpp
#include "M5Unified.h"

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Funciona em QUALQUER placa M5Stack
    M5.Display.println("Hello GESTUUM");
    int bat = M5.Power.getBatteryLevel();
}
```

**Migracoes criticas de API:**

| API Legada (Plus) | API Atual (Plus2/Unified) |
|---|---|
| `M5.Axp.GetBatVoltage()` | `M5.Power.getBatteryVoltage()` |
| `M5.Axp.SetLed(1)` | `M5.Power.setLed(1)` |
| `M5.Lcd.xxx()` | `M5.Display.xxx()` |
| `M5.Beep.tone()` | `M5.Speaker.tone()` |

### 6.2 PlatformIO vs Arduino IDE

| Aspecto | Arduino IDE | PlatformIO (VSCode) |
|---|---|---|
| **Recomendacao** | Iniciantes | **Profissional -- preferido** |
| **Gerenciamento de dependencias** | Manual, propenso a conflitos | Automatico via `platformio.ini` |
| **Estrutura de projeto** | Arquivo unico | Modular (`_manager.h`, `_handler.h`) |
| **PSRAM** | Ativar manualmente no menu | Flag no `platformio.ini` |
| **Particoes** | Menu de selecao | Declaracao explicita no ini |
| **Compilacao** | Lenta, arquivo unico | Incremental, paralela |
| **Atualizacoes** | Podem quebrar silenciosamente | Versionamento fixo |

**platformio.ini minimo para M5StickC PLUS2:**

```ini
[env:m5stick-c-plus2]
platform = espressif32
board = m5stick-c-plus2
framework = arduino
monitor_speed = 115200
upload_speed = 1500000

; OBRIGATORIO: Habilitar PSRAM
build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue

; OBRIGATORIO: Usar particao de 8MB
board_build.partitions = default_8MB.csv
board_upload.maximum_size = 8388608

lib_deps =
    m5stack/M5Unified
```

### 6.3 PSRAM: Habilitacao, Bug de Cache, Flags

**O que e:** 2 MB de RAM externa (Pseudo-Static RAM) integrada no SiP ESP32-PICO-V3-02.
Permite buffers graficos pesados e alocacoes grandes que nao cabem nos 520 KB de SRAM interna.

**Habilitacao:**

| Ambiente | Como Habilitar |
|---|---|
| Arduino IDE | Menu: Tools > PSRAM > Enabled |
| PlatformIO | `build_flags = -DBOARD_HAS_PSRAM` |

**Bug de cache PSRAM (CRITICO):**

A Espressif identificou um erratum de hardware no ESP32-PICO-V3-02 onde as linhas de cache
espelhadas operadas de forma assincrona pelos dois cores podem colidir, causando:
- Violacoes de ponteiro
- Kernel panics sem mensagem clara
- Travamentos aleatorios em operacoes pesadas de memoria
- Corrupcao de dados em threads concorrentes

**Mitigacao OBRIGATORIA:**

```ini
# PlatformIO -- adicionar AMBAS as flags
build_flags =
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
```

A flag `-mfix-esp32-psram-cache-issue` instrui o compilador/linker a isolar chamadas
sobrepostas entre cores, contornando o limite de silicio.

> **SEM esta flag, firmwares que usam PSRAM ativamente terao travamentos aleatorios
> e inexplicaveis, especialmente sob carga com Wi-Fi + display + sensores.**

### 6.4 Particoes Flash 8MB

O PLUS2 tem 8 MB de flash, mas muitas ferramentas assumem 4 MB por padrao.

**Problema:** Programas grandes (com OTA, SPIFFS, bibliotecas de imagem) excedem o limite
de 4 MB e falham com erros de alocacao de memoria.

**Solucao por ambiente:**

| Ambiente | Configuracao |
|---|---|
| **PlatformIO** | `board_build.partitions = default_8MB.csv` + `board_upload.maximum_size = 8388608` |
| **Arduino IDE** | Menu: Tools > Partition Scheme > "8M with spiffs" |

**Esquema de particoes tipico para 8MB:**

```
# default_8MB.csv
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x5000,
otadata,   data, ota,     0xe000,   0x2000,
app0,      app,  ota_0,   0x10000,  0x330000,
app1,      app,  ota_1,   0x340000, 0x330000,
spiffs,    data, spiffs,  0x670000, 0x190000,
```

### 6.5 Watchdog Timer (WDT)

Mecanismo de salvaguarda autonoma para situacoes onde o firmware trava (loop infinito,
deadlock, ponteiros corrompidos) e nao ha acesso fisico ao dispositivo.

**Funcionamento:**
1. WDT e um contador decrescente independente rodando no hardware
2. O firmware deve "alimentar" o WDT periodicamente (`wdt.feed()` / `Watchdog.reset()`)
3. Se o firmware travar e nao alimentar o WDT, o contador chega a zero
4. WDT forca reset de hardware (PUC Reset) via interrupcao nao-mascaravel
5. Dispositivo reinicia do bootloader (registrado como `OWDT_RESET`)

**Implementacao em C++ (Arduino):**

```cpp
#include <Adafruit_SleepyDog.h>

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    // Habilitar watchdog com timeout de 4 segundos
    Watchdog.enable(4000);
}

void loop() {
    // Logica principal do firmware
    M5.update();
    processar_gestos();
    enviar_audio();

    // OBRIGATORIO: alimentar o watchdog no final do loop
    Watchdog.reset();

    // ATENCAO: NAO colocar wdt.reset() dentro de ISR
    // O reset deve ser na main thread para validar saude do loop principal
}
```

**Implementacao em MicroPython:**

```python
from machine import WDT

# Timeout de 2 segundos
wdt = WDT(timeout=2000)

while True:
    # Logica principal
    processar()

    # Alimentar watchdog
    wdt.feed()
    # NAO usar delays longos que excedam o timeout
```

**Regras criticas:**
- NAO colocar `Watchdog.reset()` dentro de rotinas de interrupcao (ISR)
- O reset deve ser na main thread para confirmar saude real do loop principal
- Timeout deve ser maior que o pior caso de execucao do loop, mas nao excessivamente longo
- Para o GESTUUM: considerar 4-8 segundos (tempo suficiente para leitura de sensores + BLE)

---

## Tabela Resumo de GPIOs

| GPIO | Funcao Principal | Funcao Secundaria | Tipo | Strapping | Observacoes |
|---|---|---|---|---|---|
| 0 | Mic SPM1423 CLK | Boot mode select | I/O | **SIM** | CRITICO: conflito mic/boot |
| 2 | Buzzer (PWM) | -- | I/O | **SIM** | Deve ser LOW/flutuante no boot |
| 4 | **HOLD (Power Latch)** | -- | Output | Nao | CRITICO: deve ser HIGH para manter ligado |
| 5 | Display CS | -- | Output | **SIM** | -- |
| 12 | Display RST | Flash voltage select | Output | **SIM** | NAO alterar externamente |
| 13 | Display CLK | -- | Output | Nao | -- |
| 14 | Display DC | -- | Output | Nao | -- |
| 15 | Display MOSI | Debug UART enable | Output | **SIM** | -- |
| 19 | LED Vermelho / IR TX | -- | Output | Nao | Logica invertida. Compartilhado |
| 21 | I2C SDA (interno) | -- | I/O | Nao | MPU6886 + BM8563 |
| 22 | I2C SCL (interno) | -- | I/O | Nao | MPU6886 + BM8563 |
| 27 | Display Backlight | -- | Output | Nao | PWM para brilho |
| 32 | I2C SDA (Grove ext) | ADC1_CH4 | I/O | Nao | Max 3.3V! |
| 33 | I2C SCL (Grove ext) | ADC1_CH5 | I/O | Nao | Max 3.3V! |
| 34 | Mic SPM1423 DATA | -- | Input-only | Nao | -- |
| 37 | **Botao A** | -- | Input-only | Nao | Pull-up HW, logica invertida |
| 38 | **Bateria ADC** | -- | Input-only | Nao | Divisor 2:1 |
| 39 | **Botao B** | -- | Input-only | Nao | Pull-up HW, logica invertida |

---

## Referencias

Documentacao compilada a partir de:
- M5Stack Oficial: https://docs.m5stack.com/en/core/M5StickC%20PLUS2
- ESP32-PICO-V3-02 Datasheet: Espressif Systems
- M5Stack Community Forum
- ESP32 Forum (esp32.com)
- PlatformIO Community
- Home Assistant / ESPHome Community
- GitHub Issues: m5stack/M5Unified, m5stack/M5GFX, platformio/platform-espressif32
# GESTUUM -- Documentacao Tecnica dos Componentes (Sensor A)

Compilado a partir dos datasheets oficiais em 2026-04-03.

---

## 7. ESP32-PICO-V3-02

**Fonte:** ESP32-PICO Series Datasheet v1.1 (Espressif)

### 7.1 Especificacoes Gerais

| Parametro | Valor |
|-----------|-------|
| SoC | ESP32 chip revision v3.0/v3.1 |
| CPU | Dual-core Xtensa 32-bit LX6, ate 240 MHz |
| ROM | 448 KB |
| SRAM | 520 KB |
| RTC SRAM | 16 KB |
| Flash (in-package) | 8 MB (Quad SPI) |
| PSRAM (in-package) | 2 MB (Quad SPI) |
| Flash endurance | >100.000 ciclos P/E, >20 anos retencao |
| Tensao operacao | 3.0 -- 3.6 V |
| Temperatura operacao | -40 a +85 C |
| Dimensoes (SiP) | 7.0 x 7.0 x 1.11 mm |

### 7.2 GPIOs -- Disponibilidade e Restricoes

O ESP32-PICO-V3-02 expoe 31 GPIOs no package. Restricoes importantes:

#### 7.2.1 Pinos Input-Only (sem pull-up/pull-down, SEM saida)

| Pino | GPIO | Funcao Analogica |
|------|------|-----------------|
| 5 | GPIO36 (SENSOR_VP) | ADC1_CH0, RTC_GPIO0 |
| 6 | GPIO37 (SENSOR_CAPP) | ADC1_CH1, RTC_GPIO1 |
| 7 | GPIO38 (SENSOR_CAPN) | ADC1_CH2, RTC_GPIO2 |
| 8 | GPIO39 (SENSOR_VN) | ADC1_CH3, RTC_GPIO3 |
| 10 | GPIO34 (VDET_1) | ADC1_CH6, RTC_GPIO4 |
| 11 | GPIO35 (VDET_2) | ADC1_CH7, RTC_GPIO5 |

**ATENCAO:** Estes 6 pinos so podem ser usados como INPUT. Nao possuem resistores de pull-up/pull-down internos.

#### 7.2.2 Strapping Pins (nivel logico importa no boot)

| Pino | Default | Funcao no Boot |
|------|---------|---------------|
| GPIO0 | Pull-up (1) | Boot mode: 1=SPI Boot (normal), 0=Download |
| GPIO2 | Pull-down (0) | Boot mode (com GPIO0) |
| MTDI (GPIO12) | Pull-down (0) | VDD_SDIO: 0=3.3V, 1=1.8V |
| MTDO (GPIO15) | Pull-up (1) | U0TXD printing: 1=enabled |
| GPIO5 | Pull-up (1) | SDIO slave timing |

**CUIDADO com GPIO0:** Usado como strapping pin. Qualquer periferico conectado a GPIO0 (ex: microfone PDM) pode interferir no boot se puxar o pino para LOW durante reset.

#### 7.2.3 Pinos Ocupados pelo Flash/PSRAM (NAO USAR)

| Pino | Nome | Uso Interno |
|------|------|-------------|
| 30 | CMD/IO11 | Flash CS |
| 31 | CLK/IO6 | Flash CLK |
| 28 | SD2/IO9 | PSRAM CS |
| 29 | SD3/IO10 | PSRAM CLK |

Estes 4 GPIOs estao conectados ao flash e PSRAM internos e **NAO devem ser usados para outros fins**.

#### 7.2.4 Pinos Livres para Uso Geral (ESP32-PICO-V3-02)

| Pino | GPIO | ADC | Touch | Outras Funcoes |
|------|------|-----|-------|---------------|
| 12 | GPIO32 | ADC1_CH4 | T9 | 32K_XP |
| 13 | GPIO33 | ADC1_CH5 | T8 | 32K_XN |
| 14 | GPIO25 | ADC2_CH8 | -- | DAC_1 |
| 15 | GPIO26 | ADC2_CH9 | -- | DAC_2 |
| 16 | GPIO27 | ADC2_CH7 | T7 | -- |
| 17 | GPIO14 | ADC2_CH6 | T6 | HSPICLK, MTMS |
| 18 | GPIO12 | ADC2_CH5 | T5 | HSPIQ, MTDI (strapping!) |
| 20 | GPIO13 | ADC2_CH4 | T4 | HSPID, MTCK |
| 21 | GPIO15 | ADC2_CH3 | T3 | HSPICS0, MTDO (strapping!) |
| 22 | GPIO2 | ADC2_CH2 | T2 | HSPIWP (strapping!) |
| 23 | GPIO0 | ADC2_CH1 | T1 | CLK_OUT1 (strapping!) |
| 24 | GPIO4 | ADC2_CH0 | T0 | HSPIHD |
| 27 | GPIO20 | -- | -- | Livre |
| 32 | GPIO7 | -- | -- | SD_DATA0, U2RTS |
| 33 | GPIO8 | -- | -- | SD_DATA1, U2CTS |
| 34 | GPIO5 | -- | -- | VSPICS0 (strapping!) |
| 38 | GPIO19 | -- | -- | VSPIQ, U0CTS |
| 39 | GPIO22 | -- | -- | VSPIWP, U0RTS |
| 40 | GPIO3 | -- | -- | U0RXD (UART debug) |
| 41 | GPIO1 | -- | -- | U0TXD (UART debug) |
| 42 | GPIO21 | -- | -- | VSPIHD |

#### 7.2.5 ADC -- Restricoes

- **ADC1** (6 canais): GPIO32-39 -- funciona sempre
- **ADC2** (10 canais): GPIO0, 2, 4, 12-15, 25-27 -- **NAO FUNCIONA quando Wi-Fi esta ativo**
- Resolucao: 12-bit SAR
- Faixa efetiva com atten=3: 150--2450 mV (erro +/-60 mV apos calibracao)
- DNL: +/-7 LSB; INL: +/-12 LSB

### 7.3 Wi-Fi

| Parametro | Valor |
|-----------|-------|
| Padrao | 802.11 b/g/n |
| Modo | 1T1R, ate 150 Mbps (HT40) |
| Frequencia | 2412 -- 2484 MHz |
| TX potencia max (11b) | 19.5 dBm |
| TX potencia max (HT40 MCS7) | 13.0 dBm |
| RX sensibilidade (11b 1Mbps) | -98 dBm (typ) |
| Guard interval | 0.4 us suportado |

**Coexistencia Wi-Fi + Bluetooth:** O ESP32 compartilha a antena entre Wi-Fi e BT via mecanismo de arbitragem de tempo. Ambos operam na faixa 2.4 GHz. O ESP-IDF gerencia a coexistencia automaticamente, mas ha impacto no throughput de ambos quando ativos simultaneamente.

### 7.4 Bluetooth

| Parametro | Valor |
|-----------|-------|
| Versao | Bluetooth v4.2 BR/EDR + BLE |
| Classes | Class-1, Class-2, Class-3 |
| Audio codecs | CVSD, SBC |
| AFH | Adaptive Frequency Hopping suportado |
| BLE TX potencia | 0 a +9 dBm (configuravel) |
| BLE RX sensibilidade | -97 dBm (typ) |

**Restricoes BT Classic (A2DP):**
- A2DP (audio streaming) usa BT Classic, nao BLE
- Codec SBC e obrigatorio; aptX nao e suportado nativamente
- Audio output via I2S (DAC externo) ou DAC interno (8-bit, qualidade limitada)

### 7.5 Errata Conhecida -- GPIO36/39 com ADC + Wi-Fi

**BUG CRITICO (documentado no ESP32 ECO v3 errata):**

Quando Wi-Fi ou Bluetooth esta ativo, leituras ADC nos GPIO36 e GPIO39 apresentam "chattering" -- valores aleatorios que oscilam sem input real. Isso ocorre porque o SAR ADC e compartilhado entre Wi-Fi e o usuario, e interrupcoes do radio podem corromper a leitura.

**Mitigacao:**
1. Usar ADC1 nos canais GPIO32-35 quando Wi-Fi estiver ativo (menos afetados)
2. Evitar ADC2 completamente com Wi-Fi ligado (nao funciona)
3. Para GPIO36/39: fazer media de multiplas leituras e aplicar filtro
4. Considerar ADC externo (via I2C/SPI) para leituras criticas durante operacao RF

### 7.6 Modos de Sleep e Consumo

| Modo | Descricao | Consumo Tipico |
|------|-----------|---------------|
| Active (Wi-Fi TX 11b) | RF transmitindo | 370 mA pico |
| Active (Wi-Fi TX HT40) | RF transmitindo | 205 mA pico |
| Active (Wi-Fi RX) | RF recebendo | 113--120 mA |
| Modem-sleep (CPU 240 MHz) | Wi-Fi off, CPU on | 30--68 mA |
| Modem-sleep (CPU 80 MHz) | Wi-Fi off, CPU on | 20--31 mA |
| Light-sleep | CPU pausado, RAM mantida | 0.8 mA |
| Deep-sleep (ULP + RTC mem) | So ULP ativo | 150 uA |
| Deep-sleep (ULP sensor 1%) | ULP duty-cycled | 100 uA |
| Deep-sleep (RTC timer + mem) | Timer rodando | 10 uA |
| Deep-sleep (RTC timer only) | Minimo | 5 uA |
| Power off (CHIP_PU low) | Desligado | 1 uA |

### 7.7 Boot Sequence

1. **Power-on** -- tensao sobe, CHIP_PU (EN) vai para HIGH
2. **Strapping latch** -- chip le GPIO0, GPIO2, GPIO5, GPIO12, GPIO15 dentro de 1 ms apos CHIP_PU=HIGH
3. **Boot mode** decidido:
   - GPIO0=1, GPIO2=qualquer -> **SPI Boot** (normal, executa do flash)
   - GPIO0=0, GPIO2=0 -> **Download Boot** (UART/SDIO para programar)
4. **VDD_SDIO** configurado: GPIO12=0 -> 3.3V, GPIO12=1 -> 1.8V
5. **Bootloader** carrega do flash, inicializa CPU, chama app_main()

**Nota:** `uart_download_dis` (eFuse) desabilita permanentemente Download Boot (v3.0+).

---

## 8. MPU-6886 (IMU 6-Axis)

**Fonte:** MPU-6886 Datasheet DS-000193 Rev 1.1 (InvenSense/TDK)

### 8.1 Visao Geral

| Parametro | Valor |
|-----------|-------|
| Tipo | 6-axis MotionTracking (3-axis gyro + 3-axis accel) |
| Package | 3 x 3 x 0.75 mm, 24-pin LGA |
| VDD | 1.71 -- 3.45 V |
| VDDIO | 1.71 -- 3.45 V |
| Temperatura operacao | -40 a +85 C |
| ADC | 16-bit por eixo |
| FIFO | 1 kB |
| Shock tolerance | 20.000 g |
| Interface | I2C (400 kHz) ou SPI (10 MHz) |
| I2C Address | 0x68 (SA0=0) ou 0x69 (SA0=1) |
| WHO_AM_I | 0x19 |

### 8.2 Acelerometro

| Parametro | Valor |
|-----------|-------|
| Full-Scale Range | +/-2g, +/-4g, +/-8g, +/-16g (programavel) |
| ADC | 16-bit, complemento de 2 |
| Sensitivity (2g) | 16.384 LSB/g |
| Sensitivity (4g) | 8.192 LSB/g |
| Sensitivity (8g) | 4.096 LSB/g |
| Sensitivity (16g) | 2.048 LSB/g |
| Tolerancia sensibilidade | +/-1% |
| Zero-G offset (component) | +/-65 mg (typ +/-25 mg) |
| Zero-G offset (board-level) | +/-100 mg (typ +/-40 mg) |
| Noise density (10 Hz) | 100 ug/sqrt(Hz) typ, 170 max |
| RMS noise (BW=100 Hz) | 1.0 mg typ, 1.7 max |
| LPF range | 5.1 -- 1046 Hz |
| ODR (low-noise) | 3.91 -- 4000 Hz |
| ODR (low-power) | 3.91 -- 500 Hz |
| Startup time (from sleep) | 10 ms typ, 20 ms max |

### 8.3 Giroscopio

| Parametro | Valor |
|-----------|-------|
| Full-Scale Range | +/-250, +/-500, +/-1000, +/-2000 dps (programavel) |
| ADC | 16-bit, complemento de 2 |
| Sensitivity (250 dps) | 131 LSB/dps |
| Sensitivity (500 dps) | 65.5 LSB/dps |
| Sensitivity (1000 dps) | 32.8 LSB/dps |
| Sensitivity (2000 dps) | 16.4 LSB/dps |
| Tolerancia sensibilidade | +/-1% |
| ZRO offset | +/-10 dps (typ +/-1 dps) |
| ZRO vs temperatura | +/-0.05 dps/C (typ +/-0.01) |
| Rate noise density (10 Hz) | 0.004 dps/sqrt(Hz) typ |
| Total RMS noise (BW=100 Hz) | 0.04 dps typ |
| LPF range | 5 -- 8173 Hz |
| ODR (low-noise) | 3.91 -- 8000 Hz |
| ODR (low-power) | 3.91 -- 333.33 Hz |
| Startup time | 35 ms typ, 100 ms max |

### 8.4 Filtros Digitais (DLPF) -- Giroscopio

Configuracao via registros CONFIG (0x1A) e GYRO_CONFIG (0x1B).

**Condicao:** FCHOICE_B deve ser 0b00 para DLPF ativo.

| FCHOICE_B | DLPF_CFG | Gyro 3dB BW (Hz) | Noise BW (Hz) | Sample Rate (kHz) | Temp BW (Hz) |
|-----------|----------|-------------------|---------------|-------------------|-------------|
| x1 | x | 8173 | 8595 | 32 | 4000 |
| 10 | x | 3281 | 3451 | 32 | 4000 |
| 00 | 0 | 250 | 306.6 | 8 | 4000 |
| 00 | 1 | 176 | 177 | 1 | 188 |
| 00 | 2 | 92 | 108.6 | 1 | 98 |
| 00 | 3 | 41 | 59 | 1 | 42 |
| 00 | 4 | 20 | 30.5 | 1 | 20 |
| 00 | 5 | 10 | 15.6 | 1 | 10 |
| 00 | 6 | 5 | 8.0 | 1 | 5 |
| 00 | 7 | 3281 | 3451 | 8 | 4000 |

### 8.5 Filtros Digitais (DLPF) -- Acelerometro

Configuracao via registro ACCEL_CONFIG2 (0x1D).

**Low-Noise Mode (ACCEL_FCHOICE_B=0):**

| A_DLPF_CFG | 3dB BW (Hz) | Noise BW (Hz) | Rate (kHz) |
|------------|-------------|---------------|-----------|
| 0 | 218.1 | 235.0 | 1 |
| 1 | 218.1 | 235.0 | 1 |
| 2 | 99.0 | 121.3 | 1 |
| 3 | 44.8 | 61.5 | 1 |
| 4 | 21.2 | 31.0 | 1 |
| 5 | 10.2 | 15.5 | 1 |
| 6 | 5.1 | 7.8 | 1 |
| 7 | 420.0 | 441.6 | 1 |

**Bypass (ACCEL_FCHOICE_B=1):** 1046 Hz BW, 4 kHz rate.

### 8.6 Sample Rate e ODR

Formula:

```
SAMPLE_RATE = INTERNAL_SAMPLE_RATE / (1 + SMPLRT_DIV)
```

Onde `INTERNAL_SAMPLE_RATE` = 1 kHz (quando DLPF ativo, FCHOICE_B=00, DLPF_CFG=1..6).

Exemplos de ODR configuravel: 3.91, 7.81, 15.63, 31.25, 62.50, 125, 250, 500, 1000 Hz.

**Sem DLPF (FCHOICE_B != 00):** sample rate interno e 8 kHz ou 32 kHz.

### 8.7 FIFO

| Parametro | Valor |
|-----------|-------|
| Tamanho | 1024 bytes (1 kB) |
| Dados configuráveis | Gyro X/Y/Z, Accel X/Y/Z, Temp |
| Bytes por sample completo | 14 bytes (accel=6 + temp=2 + gyro=6) |
| Samples max no FIFO | 73 (com 6-axis + temp) |
| Modo overflow | Configuravel: parar ou sobrescrever (FIFO_MODE bit) |
| Watermark | Programavel via FIFO_WM_TH (10-bit) |
| Burst read | Suportado |

**Registro FIFO_EN (0x23):**
- Bit 4: GYRO_FIFO_EN -- habilita gyro X/Y/Z no FIFO
- Bit 3: ACCEL_FIFO_EN -- habilita accel X/Y/Z no FIFO

### 8.8 Registros Principais

#### Mapa de Registros (Resumo)

| Addr (hex) | Dec | Nome | Tipo | Descricao |
|------------|-----|------|------|-----------|
| 0x04-0x0B | 4-11 | xG/YG/ZG_OFFS_TC | R/W | Offset temp compensation gyro |
| 0x0D-0x0F | 13-15 | SELF_TEST_x_ACCEL | R/W | Self-test accel (factory) |
| 0x13-0x18 | 19-24 | xG_OFFS_USR H/L | R/W | User gyro offset (bias removal) |
| 0x19 | 25 | SMPLRT_DIV | R/W | Sample rate divider |
| 0x1A | 26 | CONFIG | R/W | DLPF config, FIFO mode, FSYNC |
| 0x1B | 27 | GYRO_CONFIG | R/W | Gyro FSR, self-test, FCHOICE |
| 0x1C | 28 | ACCEL_CONFIG | R/W | Accel FSR, self-test |
| 0x1D | 29 | ACCEL_CONFIG2 | R/W | Accel DLPF, averaging, FCHOICE |
| 0x1E | 30 | LP_MODE_CFG | R/W | Gyro low-power cycle, averaging |
| 0x20-0x22 | 32-34 | ACCEL_WOM_x_THR | R/W | Wake-on-motion threshold X/Y/Z |
| 0x23 | 35 | FIFO_EN | R/W | Selecao dados no FIFO |
| 0x37 | 55 | INT_PIN_CFG | R/W | Config pino INT (level, open-drain, latch) |
| 0x38 | 56 | INT_ENABLE | R/W | Habilita interrupcoes (WOM, FIFO, DRDY) |
| 0x3A | 58 | INT_STATUS | R (clear) | Status das interrupcoes |
| 0x3B-0x40 | 59-64 | ACCEL_xOUT H/L | R | Dados acelerometro X/Y/Z |
| 0x41-0x42 | 65-66 | TEMP_OUT H/L | R | Dados temperatura |
| 0x43-0x48 | 67-72 | GYRO_xOUT H/L | R | Dados giroscopio X/Y/Z |
| 0x50-0x52 | 80-82 | SELF_TEST_x_GYRO | R/W | Self-test gyro (factory) |
| 0x60-0x61 | 96-97 | FIFO_WM_TH 1/2 | R/W | FIFO watermark threshold |
| 0x68 | 104 | SIGNAL_PATH_RESET | R/W | Reset accel/temp signal path |
| 0x69 | 105 | ACCEL_INTEL_CTRL | R/W | Wake-on-motion intelligence |
| 0x6A | 106 | USER_CTRL | R/W | FIFO enable/reset, signal reset |
| 0x6B | 107 | PWR_MGMT_1 | R/W | Device reset, sleep, cycle, clock |
| 0x6C | 108 | PWR_MGMT_2 | R/W | Standby individual axes |
| 0x70 | 112 | I2C_IF | R/W | Desabilita I2C (para SPI-only) |
| 0x72-0x73 | 114-115 | FIFO_COUNT H/L | R | Bytes no FIFO |
| 0x74 | 116 | FIFO_R_W | R/W | Leitura/escrita FIFO |
| 0x75 | 117 | WHO_AM_I | R | Device ID (default: 0x19) |
| 0x77-0x7E | 119-126 | xA_OFFSET H/L | R/W | Accel offset registers |

#### Detalhes dos Registros Criticos

**CONFIG (0x1A):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 7 | -- | Default=1, setar para 0 |
| 6 | FIFO_MODE | 0=circular (sobrescreve), 1=para quando cheio |
| 5:3 | EXT_SYNC_SET | FSYNC sampling (0=disabled) |
| 2:0 | DLPF_CFG | Seleciona filtro gyro/temp (ver tabela 8.4) |

**GYRO_CONFIG (0x1B):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 7 | XG_ST | Self-test eixo X |
| 6 | YG_ST | Self-test eixo Y |
| 5 | ZG_ST | Self-test eixo Z |
| 4:3 | FS_SEL | 00=250dps, 01=500, 10=1000, 11=2000 |
| 1:0 | FCHOICE_B | 00=DLPF ativo, outros=bypass |

**ACCEL_CONFIG (0x1C):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 7 | XA_ST | Self-test eixo X |
| 6 | YA_ST | Self-test eixo Y |
| 5 | ZA_ST | Self-test eixo Z |
| 4:3 | ACCEL_FS_SEL | 00=2g, 01=4g, 10=8g, 11=16g |

**ACCEL_CONFIG2 (0x1D):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 5:4 | DEC2_CFG | Low-power averaging: 0=4x, 1=8x, 2=16x, 3=32x |
| 3 | ACCEL_FCHOICE_B | 0=DLPF ativo, 1=bypass (1046 Hz) |
| 2:0 | A_DLPF_CFG | Seleciona filtro accel (ver tabela 8.5) |

**PWR_MGMT_1 (0x6B):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 7 | DEVICE_RESET | 1=reset todos os registros |
| 6 | SLEEP | 1=sleep mode |
| 5 | CYCLE | 1=cycle mode (duty-cycled accel) |
| 4 | GYRO_STANDBY | 1=gyro drive on, sensing off |
| 3 | TEMP_DIS | 1=desabilita sensor temperatura |
| 2:0 | CLKSEL | 0=internal 20 MHz, 1-5=auto-select best |

**PWR_MGMT_2 (0x6C):**

| Bit | Nome | Funcao |
|-----|------|--------|
| 5 | STBY_XA | 1=accel X em standby |
| 4 | STBY_YA | 1=accel Y em standby |
| 3 | STBY_ZA | 1=accel Z em standby |
| 2 | STBY_XG | 1=gyro X em standby |
| 1 | STBY_YG | 1=gyro Y em standby |
| 0 | STBY_ZG | 1=gyro Z em standby |

### 8.9 Power Management -- Modos de Operacao

| Modo | Gyro | Accel | Consumo |
|------|------|-------|---------|
| 1 - Sleep | Off | Off | 6 uA typ, 10 max |
| 2 - Standby | Drive on | Off | -- |
| 3 - Accel Low-Power | Off | Duty-cycled | 40 uA typ (100 Hz, 1x avg) |
| 4 - Accel Low-Noise | Off | On | 321 uA typ |
| 5 - Gyro Low-Power | Duty-cycled | Off | 1.08 mA typ (100 Hz, 1x avg) |
| 6 - Gyro Low-Noise | On | Off | 2.55 mA typ |
| 7 - 6-Axis Low-Noise | On | On | 2.79 mA typ |
| 8 - 6-Axis Low-Power | Duty-cycled | On | 1.33 mA typ (100 Hz, 1x avg) |

**Nota:** O dispositivo inicia em SLEEP mode apos power-up. Precisa limpar SLEEP bit para operar.

### 8.10 I2C -- Protocolo

| Parametro | Valor |
|-----------|-------|
| Endereco padrao | 0x68 (SA0=GND) ou 0x69 (SA0=VDD) |
| Velocidade | 100--400 kHz (Fast Mode) |
| Pull-up necessario | Sim, SDA e SCL (tipico 10 kOhm para VDD) |
| Burst read | Suportado (auto-increment) |
| Burst write | Suportado (auto-increment) |

**Sequencia de leitura (single byte):**
```
START -> [0x68+W] -> ACK -> [reg_addr] -> ACK -> START -> [0x68+R] -> ACK -> [DATA] -> NACK -> STOP
```

**Sequencia de escrita (single byte):**
```
START -> [0x68+W] -> ACK -> [reg_addr] -> ACK -> [DATA] -> ACK -> STOP
```

### 8.11 Self-Test e Calibracao

**Self-test:**
1. Habilitar self-test nos registros GYRO_CONFIG (bits 7:5) e ACCEL_CONFIG (bits 7:5)
2. Ler dados com self-test ativo
3. Desabilitar self-test
4. Calcular: `SELF_TEST_RESPONSE = output_enabled - output_disabled`
5. Comparar com limites da especificacao (factory trim via OTP)

**Calibracao de offset:**
- Giroscopio: registros XG/YG/ZG_OFFS_USR (0x13-0x18) -- 16-bit, somado ao dado antes do registro de saida
- Acelerometro: registros XA/YA/ZA_OFFSET (0x77-0x7E) -- 15-bit
- Temperatura compensation: automatica via registros TC (carregados do factory trim no power-up)

**Sensor de temperatura:**
```
Temp_degC = (TEMP_OUT / 326.8) + 25.0
```

---

## 9. BM8563 (RTC)

**Fonte:** BM8563 V1.1 Datasheet (Belling)

### 9.1 Visao Geral

| Parametro | Valor |
|-----------|-------|
| Funcao | Real-Time Clock / Calendar com I2C |
| Fabricante | Belling (Shanghai Belling) |
| VDD | 1.5 -- 5.5 V (I2C requer min 1.8V) |
| Consumo standby | 250 nA typ (3.0V, 25C, CLKOUT off) |
| Oscilador | 32.768 kHz (cristal externo ou interno) |
| I2C Address | 0x51 (write: 0xA2, read: 0xA3) |
| I2C velocidade max | 400 kHz |
| Temperatura operacao | -40 a +85 C |
| Package | SOP8 / MSOP8 / TSSOP8 |

### 9.2 Pinos

| Pino | Simbolo | Funcao |
|------|---------|--------|
| 1 | OSCI | Entrada oscilador (cristal 32.768 kHz) |
| 2 | OSCO | Saida oscilador |
| 3 | INT | Saida interrupcao (open-drain, ativo LOW) |
| 4 | VSS | GND |
| 5 | SDA | I2C dados (open-drain) |
| 6 | SCL | I2C clock (input) |
| 7 | CLKOUT | Saida clock programavel (open-drain) |
| 8 | VDD | Alimentacao positiva |

### 9.3 Registros de Data/Hora

Todos os valores em formato BCD (exceto weekday).

| Endereco | Nome | Bits Uteis | Range | Notas |
|----------|------|-----------|-------|-------|
| 0x00 | Control/Status 1 | TEST1, STOP, TESTC | -- | STOP=1 para relógio |
| 0x01 | Control/Status 2 | TI/TP, AF, TF, AIE, TIE | -- | Flags interrupcao |
| 0x02 | Seconds | [6:0] | 00--59 BCD | Bit7=VL (power-down flag) |
| 0x03 | Minutes | [6:0] | 00--59 BCD | -- |
| 0x04 | Hours | [5:0] | 00--23 BCD | 24h format |
| 0x05 | Days | [5:0] | 01--31 BCD | -- |
| 0x06 | Weekday | [2:0] | 0--6 | 0=domingo |
| 0x07 | Month/Century | [4:0] month, bit7=C | 01--12 BCD | C=0 -> 20xx, C=1 -> 19xx |
| 0x08 | Year | [7:0] | 00--99 BCD | -- |

**Flag VL (bit 7 do registro Seconds):**
- VL=0: relogio operando normalmente, dados validos
- VL=1: houve queda de alimentacao, dados podem estar incorretos

### 9.4 Registros de Alarme

| Endereco | Nome | Bits Uteis | Range | Notas |
|----------|------|-----------|-------|-------|
| 0x09 | Minute Alarm | [6:0] + AE(bit7) | 00--59 BCD | AE=0: alarme ativo |
| 0x0A | Hour Alarm | [5:0] + AE(bit7) | 00--23 BCD | AE=0: alarme ativo |
| 0x0B | Day Alarm | [5:0] + AE(bit7) | 01--31 BCD | AE=0: alarme ativo |
| 0x0C | Weekday Alarm | [2:0] + AE(bit7) | 0--6 | AE=0: alarme ativo |

**Logica:** Quando AE=0 (ativo) e o valor BCD coincide com o tempo atual, o flag AF (reg 0x01 bit3) e setado. A interrupcao INT e gerada se AIE=1.

### 9.5 Timer (Countdown)

| Endereco | Nome | Funcao |
|----------|------|--------|
| 0x0E | Timer Control | TE (bit7): 0=off, 1=on; TD[1:0]: clock source |
| 0x0F | Timer Value | 8-bit countdown value (n) |

**Clock source (TD[1:0]):**

| TD1 | TD0 | Frequencia |
|-----|-----|-----------|
| 0 | 0 | 4096 Hz |
| 0 | 1 | 64 Hz |
| 1 | 0 | 1 Hz |
| 1 | 1 | 1/60 Hz (1 por minuto) |

**Periodo do timer:** `T = n / freq_clock`

Quando countdown chega a zero, TF (reg 0x01 bit2) e setado. Se TIE=1, gera interrupcao no pino INT.

### 9.6 CLKOUT

| Endereco | Nome | Funcao |
|----------|------|--------|
| 0x0D | CLKOUT Control | FE (bit7): enable; FD[1:0]: frequencia |

| FD1 | FD0 | Saida |
|-----|-----|-------|
| 0 | 0 | 32.768 kHz |
| 0 | 1 | 1024 Hz |
| 1 | 0 | 32 Hz |
| 1 | 1 | 1 Hz |

FE=0: CLKOUT em alta impedancia. FE=1: CLKOUT ativo (open-drain, precisa pull-up).

---

## 10. SPM1423HM4H-B (Microfone MEMS)

**Fonte:** SPM1423HM4H-B Datasheet Rev A (Knowles Acoustics)

### 10.1 Visao Geral

| Parametro | Valor |
|-----------|-------|
| Tipo | Microfone MEMS digital (PDM output) |
| Fabricante | Knowles Acoustics (SiSonic) |
| Interface | PDM (Pulse Density Modulation) |
| Temperatura operacao | -40 a +100 C |
| Temperatura armazenamento | -40 a +100 C |
| Dimensoes | ~3.3 x 2.3 x 1.0 mm |
| Package | 6-pin SMD |
| MSL | Class 2a |
| RoHS | Conforme |
| Reflows max | 3 ciclos |

### 10.2 Interface PDM

O SPM1423 gera saida digital PDM (Pulse Density Modulation). No ESP32, a recepcao PDM e feita via interface I2S configurada em modo PDM.

**Conexao tipica no ESP32:**
- CLK (clock PDM) -> gerado pelo ESP32 via I2S_CLK
- DATA (saida PDM) -> recebido pelo ESP32 via I2S_DATA_IN

### 10.3 CONFLITO COM GPIO0 -- STRAPPING PIN

**ATENCAO -- PROBLEMA CRITICO NO M5StickC Plus2:**

O microfone SPM1423 no M5StickC Plus2 esta conectado ao **GPIO0**, que e um **strapping pin** do ESP32. Durante o boot, GPIO0 determina o modo de operacao:

- GPIO0 = HIGH -> SPI Boot (normal)
- GPIO0 = LOW -> Download Mode (programacao)

**Problema:** O microfone PDM pode gerar sinais no GPIO0 durante o reset/boot, potencialmente puxando o pino para LOW e causando entrada em Download Mode ao inves de boot normal.

**Mitigacoes:**
1. O clock PDM so e gerado APOS o boot, entao o microfone fica quiescente durante o strapping (na maioria dos casos funciona)
2. Se houver problemas de boot, garantir pull-up forte (10k) no GPIO0
3. Nao inicializar I2S/PDM antes da configuracao completa do sistema
4. Em design customizado: evitar GPIO0 para microfone PDM

**GPIO usado no M5StickC Plus2:**
- CLK: GPIO0 (strapping pin!)
- DATA: GPIO34 (input-only, ok para PDM data)

### 10.4 Sample Rate e Sensibilidade

O microfone PDM opera com clock fornecido pelo host (ESP32). Parametros tipicos do SPM1423:

| Parametro | Valor Tipico | Notas |
|-----------|-------------|-------|
| Clock PDM | 1.0 -- 3.25 MHz | Fornecido pelo ESP32 |
| Sample rate efetivo | Depende do decimation | Tipico: 16 kHz -- 48 kHz |
| SNR | ~65 dB (high SNR version) | "High SNR" no nome do produto |
| Sensibilidade | -26 dBFS (tipico) | A 1 kHz, 94 dB SPL |
| Direcionalidade | Omnidirecional | -- |

**No GESTUUM (config validada):**
- Clock I2S: configurado para gerar 48 kHz sample rate
- Bits per sample: 16
- Channel: mono (1 canal)
- O dado PDM e decimado internamente pelo I2S do ESP32

### 10.5 Precaucoes de Manuseio

- NAO puxar vacuo sobre o port hole (dano permanente)
- NAO lavar a placa apos reflow
- NAO usar ultrasom para limpeza
- NAO inserir objetos no port hole
- NAO aplicar pressao de ar no port hole
- Shelf life: 12 meses em embalagem selada (30C, 70% RH)

---

*Fim do compilado de datasheets -- GESTUUM Sensor A*
