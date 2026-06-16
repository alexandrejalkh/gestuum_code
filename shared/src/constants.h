/**
 * GESTUUM — Global Constants
 * Bloco: 1.1 — Setup PlatformIO + Biblioteca shared/
 * Responsibility: Define all project-wide constants, pins, and enumerations.
 */

#ifndef GESTUUM_CONSTANTS_H
#define GESTUUM_CONSTANTS_H

#include <stdint.h>

// === 3D GRID ===
// Historico: 11→7 (INSTALL-19, tremor mudava celula) → 11 (2026-04-03, Orbital absorve ruido).
// Com Orbital Signatures como match primario, a grid serve para discriminacao espacial.
// O ruido do tremor nao contamina porque o Orbital filtra candidatos antes do grid matching.
// Grid 11: celula=0.36g, boa discriminacao entre gestos similares (~50 gestos).
#define GRID_SIZE           11
#define GRID_CENTER         5
#define GRID_MIN            0
#define GRID_MAX            10

// === IMU ===
#define IMU_SAMPLE_RATE_HZ  50
#define IMU_SAMPLE_PERIOD_MS (1000 / IMU_SAMPLE_RATE_HZ)  // 20ms
#define EMA_ALPHA           0.3f
#define DAMPING_FACTOR      0.95f
// FIX INSTALL-16: Reduzido de 8.0 para 2.0. Com 8g de range, gestos normais
// (~1-2g) mal mudavam 1 celula na grid 7x7x7. A trajetoria ficava com 1-2
// pontos e nunca atingia o minimo. Com 2g, gestos de mao geram 3-5 celulas.
#define ACCEL_RANGE         2.0f  // +/-2g — sensibilidade para gestos de mao
#define IMU_MAX_ACCEL_G     16.0f // Maximum accelerometer range for validation

// FIX INSTALL-20: Gyroscope range para grid de rotacao.
// MPU6886 default: +/-250 dps. Gestos de mao: ~50-150 dps tipico.
// 200 dps mapeia a maior parte dos gestos para a grid sem saturar.
#define GYRO_RANGE          200.0f  // +/-200 graus/segundo

// FIX INSTALL-16: Deslocamento minimo da trajetoria para considerar gesto valido.
// getTotalMovement() soma distancias euclidianas entre pontos consecutivos na grid.
// Micro-tremores geram ~1-3 de deslocamento. Gestos reais geram ~5-20+.
// Sem esse threshold, qualquer tremor da mao e aceito como gesto.
// FIX INSTALL-17: Calibrado com dados reais:
// - Mao parada: ~4.0 em 1.5s (micro-tremor)
// - Gesto real: ~5.0-6.0 em 3s (levantar mao)
// 5.5 separa micro-tremor de gesto real com margem minima.
// FIX INSTALL-18: Reduzido para 5.0. Com 5.5, gestos reais ficavam
// no limite (5.0) e davam timeout. 5.0 aceita gestos reais consistentes.
// Reduzido de 5.0 para 3.0: testes reais mostram gestos de mao gerando 3.4-5.0
// de deslocamento na grid. Com 5.0, gestos validos eram rejeitados por timeout.
#define MIN_GESTURE_MOVEMENT 3.0f

// FIX INSTALL-18: Jaccard Similarity — reconhecimento deterministico.
// Compara conjuntos de celulas visitadas (nao trajetorias elasticas).
// Threshold minimo de similaridade para aceitar um gesto como match.
// 0.4 = 40% das celulas em comum. Abaixo disso, rejeita.
// FIX INSTALL-19: Threshold para comparacao por sequencia.
// 85% = 85% dos passos da sequencia devem estar na vizinhanca ±1 celula.
// Renomeado mas mantido nome JACCARD por compatibilidade (usado em processGesture).
#define JACCARD_MATCH_THRESHOLD 0.70f

// === DTW ===
#define DTW_THRESHOLD_DEFAULT   3.5f
#define DTW_THRESHOLD_EMERGENCY 4.5f
// FIX M07: Alinhado DTW_MAX_TRAJECTORY_LEN com DTW_MAX_COST_MATRIX_SIZE.
// Antes: trajetoria podia ter ate 200 pontos, mas DTW rejeitava > 150.
// Resultado: gestos longos (151-200 pontos) falhavam silenciosamente.
#define DTW_MAX_TRAJECTORY_LEN  150
// FIX INSTALL-15: Reduzido de 10 para 3. Testes reais mostram que gestos
// na grid 7x7x7 geram 2-4 pontos de trajetoria. Com 10, nenhum gesto
// real passava na validacao do addTrainingSample().
#define DTW_MIN_TRAJECTORY_LEN  3
#define DTW_MAX_COST_MATRIX_SIZE 150  // Limite da matriz DTW (N ou M)

// === GESTURES ===
#define MAX_GESTURES_PER_CATEGORY 40
#define MAX_GESTURES_TOTAL 150
#define MAX_ACTIVE_PROFILES 3  // max profiles loaded simultaneously besides base
#define GESTURE_TIMEOUT_MS  3000
#define ACTIVATION_TIMEOUT_MS 500
#define COMBO_TIMEOUT_MS    2000

// === GESTURE CAPTURE LEVELS ===
enum GestureLevel : uint8_t {
    LEVEL_LIMITED  = 0,  // Reduced mobility
    LEVEL_STANDARD = 1,  // General use
    LEVEL_ADVANCED = 2,  // LIBRAS / full mobility
    LEVEL_COUNT = 3
};

struct LevelConfig {
    uint16_t minRecordingMs;
    uint16_t gestureTimeoutMs;
    uint16_t stableDurationMs;
    float    dtwThresholdMultiplier;  // multiplied by gesture's base threshold
    float    doubleTapThreshold;
    uint8_t  minTrajectoryLen;
};

// FIX M03: extern evita duplicacao em cada .cpp que inclui o header.
// Definicao real em constants.cpp (ou qualquer .cpp que inclua o header UMA vez).
// Com static const, cada translation unit tinha sua propria copia (~72 bytes cada).
extern const LevelConfig LEVEL_CONFIGS[LEVEL_COUNT];

// === CATEGORIES ===
// NOTA AUDITOR (2026-05-02 — D-C0-02 em docs/pendencias_caminho_c.md):
// CAT_TRABALHO eh "reservado pra v2" — JSON deletado, gestos nao carregam.
// Manter por decisao do dono. TODO auditor: avaliar refator (rename pra
// CAT_RESERVED_3 ou remocao com mudanca em ~10 arquivos).
enum Category : uint8_t {
    CAT_GERAL = 0,
    CAT_EMERGENCIA = 1,
    CAT_CASA = 2,
    CAT_TRABALHO = 3,    // [v2 reservado — JSON nao existe]
    CAT_SOCIAL = 4,
    CAT_COUNT = 5
};

// === PERFIS (profiles beyond base 5 categories) ===
// NOTA AUDITOR (2026-05-02): PROFILE_HOSPITAL e PROFILE_TRANSPORTE
// "reservados pra v2" (mesma situacao acima).
enum Profile : uint8_t {
    PROFILE_BASE = 0,      // 5 base categories (always loaded)
    PROFILE_HOSPITAL = 1,    // [v2 reservado — JSON nao existe]
    PROFILE_ESCOLA = 2,
    PROFILE_TRANSPORTE = 3,  // [v2 reservado — JSON nao existe]
    PROFILE_AUTOMACAO = 4,
    PROFILE_COUNT = 5
};

// === VOZES ===
enum Voice : uint8_t {
    VOICE_HOMEM = 0,
    VOICE_MULHER = 1,
    VOICE_MENINO = 2,
    VOICE_MENINA = 3,
    VOICE_COUNT = 4
};

// === CATEGORY COLORS (RGB hex) ===
#define COLOR_GERAL      0x0000FF  // Blue
#define COLOR_EMERGENCIA 0xFF0000  // Red
#define COLOR_CASA       0x00FF00  // Green
#define COLOR_TRABALHO   0xFFFF00  // Yellow
#define COLOR_SOCIAL     0x8000FF  // Purple

// === ESP-NOW ===
// Canal ESP-NOW: evitar canais 1, 6 e 11 (mais usados por roteadores WiFi).
// Canal 13 (2.472 GHz) e permitido no Brasil e pouco congestionado.
// Reduz interferencia em escolas, hospitais e ambientes com muitos WiFi.
#define ESPNOW_CHANNEL      13
#define HEARTBEAT_INTERVAL_MS 1000
#define CONNECTION_TIMEOUT_MS 5000

// === AUDIO (HAT-SPK2 / MAX98357A) ===
#define AUDIO_SAMPLE_RATE   48000  // Match exato com saida do M5.Speaker (zero resampling)
#define AUDIO_BITS          16
#define I2S_BUFFER_COUNT    8
#define I2S_BUFFER_LEN      256

// === HAT-SPK2 I2S PINS ===
#define I2S_BCLK_PIN    26
#define I2S_LRCK_PIN    0
#define I2S_DATA_PIN    25

// === ATOMS3 LITE + LED STRIP (WS2812B) ===
// FIX INSTALL-14: GPIO2 = DIN da fita LED (confirmado pela PCB).
// Jumper nos pinos 5V, G2, G1: vermelho=5V, preto=G2(DIN), amarelo=G1(GND virtual)
#define LED_STRIP_PIN   2   // GPIO2 = DIN da fita LED
#define NUM_LEDS        30
// FIX #6: Reduzido de 180 para 150 (alinhado com BRIGHTNESS_MAX do config.h).
// 180 excedia o limite de protecao do powerbank → risco de brownout.
#define LED_BRIGHTNESS  150

// === MODELO ORBITAL ===
// Buffer de amostras brutas para extracao de features orbitais.
// FIX P-Bug2 (2026-05-02 pentest Frank): 250 → 360 (7.2s a 50Hz).
// Razao: LEVEL_ADVANCED tem gestureTimeoutMs=7000 (350 amostras).
// Com buffer 250, o ultimo 1/3 do gesto era silenciosamente truncado
// (`if (rawCount < ORBITAL_RAW_BUFFER_SIZE)` em matrix3d.cpp:49).
// 360 cobre LEVEL_ADVANCED com margem de seguranca.
// Custo: +110 samples × 12B × 4 matrices (A/B accel + A/B gyro)
// = ~5KB de RAM extra. Aceitavel — estamos em 23.4%.
#define ORBITAL_RAW_BUFFER_SIZE    360

// Deteccao de stroke: onset quando magnitude > repouso + threshold
#define ORBITAL_ONSET_THRESHOLD_G  0.3f
// Deteccao de stroke: offset quando magnitude < repouso + threshold
#define ORBITAL_OFFSET_THRESHOLD_G 0.15f
// Amostras consecutivas acima do onset para confirmar inicio
#define ORBITAL_ONSET_SAMPLES      3
// Amostras consecutivas abaixo do offset para confirmar fim (500ms a 50Hz).
// 200ms (10) causava offset prematuro por pausas breves no meio do gesto.
// 500ms (25) exige meio segundo de inatividade real para encerrar o stroke.
#define ORBITAL_OFFSET_SAMPLES     25
// Duracao minima do stroke em amostras (1s a 50Hz).
// Se stroke detectado < 50 amostras, e provavelmente um falso positivo
// (capturou preparacao ou ruido). Nesse caso, usar o buffer inteiro.
#define ORBITAL_MIN_STROKE_SAMPLES 50
// Similaridade minima orbital para aceitar match (0.0 = nada, 1.0 = identico).
// 0.75 rejeitava gestos corretos por variacao de orientacao do sensor na mao.
// 0.55 aceita variacao natural mantendo discriminacao entre gestos distintos.
#define ORBITAL_MATCH_THRESHOLD    0.55f

#endif // GESTUUM_CONSTANTS_H
