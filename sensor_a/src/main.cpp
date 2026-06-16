/**
 * GESTUUM — Sensor A Main (State Machine + Module Integration)
 * Bloco: 3.6 — Main State Machine
 * Responsibility: Orchestrate the full GESTUUM system on Sensor A (M5StickC Plus2).
 *
 * State Machine:
 *   INIT -> PAIRING -> IDLE <-> RECORDING -> MATCHING -> SPEAKING -> IDLE
 *                        ^                       |                    |
 *                        +-------- +-----------+ +-- CONTEXT_WAIT --+
 *                        |         |
 *                        +<--- EMERGENCY (toggle via BtnA hold 2s)
 *                        |
 *                        +<-> TRAINING (3 samples -> compute -> save -> IDLE)
 *
 * Configuration Mode:
 *   Activated by holding Button A+B simultaneously for 3 seconds,
 *   or by triple-clicking Button B within 1 second (UX10 accessibility).
 *   Stops ESP-NOW, starts BLE GATT server for app-based configuration.
 *   USB Serial configuration is always active regardless of mode.
 *   Exit config mode by pressing Button B -> stops BLE, restarts ESP-NOW.
 *
 * Transitions:
 *   INIT      -> PAIRING   : After setup() completes
 *   PAIRING   -> IDLE      : Sensor B heartbeat received (paired)
 *   IDLE      -> RECORDING : Double tap detected (push-to-talk)
 *   IDLE      -> EMERGENCY : Button A held 2s
 *   RECORDING -> MATCHING  : Sufficient movement after MIN_RECORDING_DURATION_MS
 *   RECORDING -> IDLE      : Timeout (GESTURE_TIMEOUT_MS) with no gesture
 *   MATCHING  -> SPEAKING  : Gesture recognized with audio, playback started
 *   MATCHING  -> IDLE      : No match found, or automation-only gesture (no audio)
 *   SPEAKING  -> IDLE      : Audio playback finished
 *   EMERGENCY -> IDLE      : Button B held 2s while in emergency
 *   IDLE      -> TRAINING  : train_start command via config_handler
 *   TRAINING  -> IDLE      : 3 samples recorded, computed, and saved (or cancelled via BtnB)
 *
 * Automation gestures run in parallel — checkAutomationGestures() is called
 * every loop iteration regardless of state machine position.
 *
 * Fixes applied:
 *   B7  — Use playFile("socorro.wav") so voice path is auto-prepended
 *   B9  — Call displayUI.forceConnectionUpdate() when ESP-NOW state changes
 *   C1  — Use espnow_getIMUData() instead of directly reading lastIMUPacket
 *   H3  — Validate acceleration within +/-IMU_MAX_ACCEL_G before feeding gesture engine
 *   M2  — Reset ALL static state in detectDoubleTap() on successful detection
 *   M4  — Check gestureEngine.isReady() after begin(); show error if not ready
 *   UX7 — Require BtnB hold 2s to exit emergency (prevents accidental cancel)
 *   UX6 — BtnA click = previous category (bidirectional navigation)
 *   UX8 — Double beep + toast overlay when Sensor B disconnects
 *   UX9 — Periodic audible alert when battery is low (<15%) or critical (<5%)
 *   UX10— Triple-click BtnB within 1s as alternative config mode entry
 */

#include <M5StickCPlus2.h>
#include <nvs_flash.h>
#include <driver/adc.h>       // FIX BTN-02: adc_power_acquire() para GPIO39 chattering
#include "config.h"
#include "constants.h"
#include "protocol.h"
#include "espnow_comm.h"
#include <esp_now.h>
#include "gesture_engine.h"
#include "audio_player.h"
#include "display_ui.h"
#include "category_manager.h"
#include "imu_reader.h"
#include "voice_manager.h"
#include "ble_config.h"
#include "serial_config.h"
#include "config_handler.h"
#include "bt_audio.h"
#include "menu_ui.h"
#include <Preferences.h>  // Sprint C3d: NVS pra modo silencioso global

// === Global State Definitions ===
SystemState currentState = STATE_INIT;
bool systemActive = false;
unsigned long recordStartTime = 0;
unsigned long lastHeartbeatSent = 0;
unsigned long lastIMURead = 0;

// === Gesture Level Global State ===
GestureLevel currentLevel = LEVEL_STANDARD;

// === Voice & Profile Global State ===
Voice currentVoice = VOICE_HOMEM;
Profile activeProfiles[MAX_ACTIVE_PROFILES] = {};
uint8_t activeProfileCount = 0;

// === Module Instances ===
// Sprint C2 (Caminho C, 2026-05-02): removido `static` para permitir
// que menu_ui.cpp acesse via `extern GestureEngine gestureEngine;`.
// Alternativa seria injecao via callback/provider — postergado por simplicidade.
GestureEngine gestureEngine;
static BTAudio btAudio;
// Sprint C3d (Caminho C, 2026-05-02): nao-static pra menu_ui.cpp acessar
// audioPlayer.isSilentAll() na tela ACTION_SILENT.
AudioPlayer audioPlayer;
static DisplayUI displayUI;
static CategoryManager categoryManager;
static IMUReader imuReader;
static VoiceManager voiceManager;

// === Emergency state flag (play audio only once on entry) ===
static bool emergencyAudioPlayed = false;

// === Post-audio cooldown (FIX FLOW-08) ===
// O speaker vibra o acelerometro no mesmo PCB. Sem cooldown, a vibracao
// residual e capturada como gesto, criando loop infinito.
static unsigned long postAudioCooldownStart = 0;
static constexpr unsigned long POST_AUDIO_COOLDOWN_MS = 500;

// === Configuration mode state ===
static bool configModeActive = false;
static unsigned long configModeStartMs = 0;  // FIX #9: timer do config mode timeout
static unsigned long _emergencyLastRetransmit = 0;  // FIX #5: retransmissao SOS
static unsigned long _emergencySosLastMs = 0;       // Timer do loop SOS Morse
static unsigned long configButtonHoldStart = 0;
static bool configButtonsHeld = false;
static const unsigned long CONFIG_HOLD_DURATION_MS = 3000;  // 3 seconds hold

// === Connection tracking for B9 fix ===
static bool lastKnownConnectionState = false;

// === Context buffer for phrase building (context + object) ===
static bool contextActive = false;
static char contextName[48] = {};
static char contextAudioFile[32] = {};
static unsigned long contextStartTime = 0;

// Modo conversa removido — codigo meio-ativado era armadilha.
// Reimplementar como feature completa quando necessario.

// === Training mode state ===
static bool trainingActive = false;
// Sprint C2 (Caminho C, 2026-05-02): true se treino foi disparado pelo menu
// local (em vez de BLE/serial). Quando termina/cancela, voltar pro menu
// (no SUB_TRAIN_LIST) em vez de IDLE — UX continua, da pra treinar varios
// gestos seguidos sem reabrir o menu toda vez.
static bool trainingReturnToMenu = false;
// Fases do treino:
//   0 = WAITING_FOR_TAP — esperando double-tap do usuario
//   1 = COUNTDOWN — contagem regressiva 3-2-1-GO (3 segundos)
//   2 = RECORDING — gravando gesto real
//   3 = SAMPLE_DONE / COMPUTING — processando amostras
//   4 = RESULT_DISPLAY — mostrando resultado do treino
static int trainingPhase = 0;
static unsigned long _trainingResultTime = 0;
static char trainingGestureId[16] = {};
static char trainingGestureName[32] = {};
static unsigned long trainingRecordStart = 0;
static unsigned long trainingCountdownStart = 0;  // FIX INSTALL-16: inicio do countdown
static int trainingLastCountdownNum = 0;           // FIX INSTALL-16: ultimo numero exibido
static SystemState trainingReturnState = STATE_IDLE;

// === Forward Declarations ===
static void handleButtons();
static void handleIMU();
static void handleHeartbeat();
static void handleState();
static void handleAutomationGestures();
static bool detectDoubleTap();
static void onCategoryChanged(Category newCat);
static bool isAccelValid(float ax, float ay, float az);
static void loadActiveProfileGestures();
static void detectConfigMode();
static uint8_t detectClickPatternB();
static void enterConfigMode();
static void exitConfigMode();
static void onSerialCommand(const char* json);
static void onBLECommand(const char* json);
static void onSerialResponse(const char* json);
static void onBLEResponse(const char* json);
static void handleBatteryAlert();
static void startTrainingMode(const char* gestureId, const char* gestureName);
static void cancelTrainingMode();
static void handleTrainingState();
static void onTrainingStartCmd(const char* gestureId, const char* gestureName);
static void onTrainingCancelCmd();

// === Battery level helper (used by ConfigHandler) ===
int getBatteryLevel() {
    return M5.Power.getBatteryLevel();
}

// ============================================================================
// isAccelValid() — Fix H3: Validate acceleration values
// ============================================================================

/**
 * Check that all acceleration components are within the valid range
 * defined by IMU_MAX_ACCEL_G (16.0g). Values outside this range indicate
 * sensor error or corrupted data and must be discarded.
 */
static bool isAccelValid(float ax, float ay, float az) {
    return (fabsf(ax) <= IMU_MAX_ACCEL_G &&
            fabsf(ay) <= IMU_MAX_ACCEL_G &&
            fabsf(az) <= IMU_MAX_ACCEL_G);
}

// ============================================================================
// loadActiveProfileGestures() — Load gestures from all active profiles
// ============================================================================

/**
 * Iterate over active profiles in VoiceManager and load their gestures
 * into the gesture engine (appended after base category gestures).
 * PROFILE_AUTOMACAO is always loaded regardless of VoiceManager state.
 */
static void loadActiveProfileGestures() {
    gestureEngine.clearProfileGestures();
    activeProfileCount = 0;

    // Always load PROFILE_AUTOMACAO first (like BASE, it is always active)
    gestureEngine.loadGesturesForProfile(PROFILE_AUTOMACAO);
    if (activeProfileCount < MAX_ACTIVE_PROFILES) {
        activeProfiles[activeProfileCount] = PROFILE_AUTOMACAO;
        activeProfileCount++;
    }

    for (uint8_t i = 1; i < PROFILE_COUNT; i++) {  // Skip PROFILE_BASE
        Profile p = static_cast<Profile>(i);

        // PROFILE_AUTOMACAO already loaded above
        if (p == PROFILE_AUTOMACAO) {
            continue;
        }

        if (voiceManager.isProfileActive(p)) {
            gestureEngine.loadGesturesForProfile(p);
            if (activeProfileCount < MAX_ACTIVE_PROFILES) {
                activeProfiles[activeProfileCount] = p;
                activeProfileCount++;
            }
        }
    }

    // Fix B1: Also load custom gestures (user-created via app)
    gestureEngine.loadCustomGestures();

    // Load context gestures for phrase building (context + object)
    gestureEngine.loadContextGestures();

    currentVoice = voiceManager.getCurrentVoice();
}

// ============================================================================
// handleAutomationGestures() — Check automation gestures every iteration
// ============================================================================

/**
 * Called every loop iteration to check for automation-only gestures
 * (gestures with automation_cmd but no audio_file). These run independently
 * of the state machine — user can trigger LED control while the system
 * is in any state (IDLE, RECORDING, SPEAKING, etc.).
 */
static void handleAutomationGestures() {
    if (!systemActive) {
        return;
    }

    AutomationResult autoResult = gestureEngine.checkAutomationGestures();
    if (autoResult.triggered) {
        // Broadcast comando de automacao via ESP-NOW.
        // Nota 2026-05-02: AtomS3 LED foi removido do projeto. Broadcast
        // continua (custo zero, sem ouvintes) — preservado pra possivel
        // re-introducao em v2 (LED indicator, atuadores domoticos, etc).
        espnow_send_automation(
            static_cast<uint8_t>(autoResult.command), 0);

        Serial.printf("[MAIN] Automation triggered: cmd=0x%02X\n",
                      static_cast<int>(autoResult.command));
    }
}

// ============================================================================
// Configuration Mode: Serial and BLE command callbacks
// ============================================================================

/**
 * Called when a JSON command is received via USB Serial.
 * Routes to ConfigHandler and sets response callback to Serial.
 */
static void onSerialCommand(const char* json) {
    configHandler.setResponseCallback(onSerialResponse);
    configHandler.handleCommand(json);
}

/**
 * Called when a JSON command is received via BLE.
 * Routes to ConfigHandler and sets response callback to BLE.
 */
static void onBLECommand(const char* json) {
    configHandler.setResponseCallback(onBLEResponse);
    configHandler.handleCommand(json);
}

/**
 * Send response back via USB Serial.
 */
static void onSerialResponse(const char* json) {
    serialConfig.sendResponse(json);
}

/**
 * Send response back via BLE.
 */
static void onBLEResponse(const char* json) {
    bleConfig.sendResponse(json);
}

// ============================================================================
// detectConfigMode() — Check if both buttons A+B held for 3 seconds
// ============================================================================

/**
 * Detect simultaneous press of Button A and Button B for CONFIG_HOLD_DURATION_MS.
 * When detected, toggle configuration mode (enter or exit).
 *
 * Note: M5StickC Plus2 buttons are active LOW. We check isPressed() for both.
 */
static void detectConfigMode() {
    bool btnAPressed = M5.BtnA.isPressed();
    bool btnBPressed = M5.BtnB.isPressed();

    if (btnAPressed && btnBPressed) {
        if (!configButtonsHeld) {
            // Both buttons just pressed — start timing
            configButtonsHeld = true;
            configButtonHoldStart = millis();
        } else {
            // Both buttons still held — check duration
            unsigned long elapsed = millis() - configButtonHoldStart;
            if (elapsed >= CONFIG_HOLD_DURATION_MS) {
                // 3 second hold detected
                configButtonsHeld = false;  // Reset to prevent retriggering

                if (!configModeActive) {
                    enterConfigMode();
                } else {
                    exitConfigMode();
                }
            }
        }
    } else {
        // One or both buttons released — reset tracking
        configButtonsHeld = false;
    }
}

// ============================================================================
// detectClickPatternB() — Sprint C1 (2026-05-02): detector unificado
// ============================================================================

/**
 * Detecta padroes 1x/2x/3x click em BtnB com timing finite-state.
 *
 * Returna apos a JANELA fechar (timeout 350ms desde ultimo click sem novo):
 *   0 = nada detectado neste frame
 *   1 = 1 click detectado
 *   2 = 2 clicks detectados
 *   3 = 3+ clicks detectados (cap em 3)
 *
 * IMPORTANTE: o retorno NAO e instantaneo no momento do click — ele e
 * adiado ate a janela expirar. Isso evita confundir 2x com 3x quando user
 * solta antes do 3o click. Latencia maxima: 350ms apos ultimo click.
 *
 * Janela de 350ms escolhida pra: humanos clicam 2-3x rapido em ~200ms,
 * 350ms da margem mas nao deixa lento. Ajustar se uso real mostrar problema.
 */
static uint8_t detectClickPatternB() {
    static uint8_t clickCount = 0;
    static unsigned long lastClickMs = 0;
    static constexpr unsigned long WINDOW_MS = 350;

    // Sprint C6 (2026-05-02): bug encontrado em analise estatica end-to-end.
    // detectClickPatternB() consumia wasClicked() mesmo em STATE_TRAINING,
    // fazendo handleTrainingState() perder o clique de cancel. Skip total
    // quando training ativo — clicks pertencem ao handleTrainingState la.
    if (currentState == STATE_TRAINING) {
        clickCount = 0;  // reset, evita arrastar contagem antiga
        return 0;
    }

    if (M5.BtnB.wasClicked()) {
        clickCount++;
        if (clickCount > 3) clickCount = 3;  // cap
        lastClickMs = millis();
        return 0;  // ainda esperando mais clicks
    }

    // Janela fechou? Retorna o padrao detectado
    if (clickCount > 0 && (millis() - lastClickMs > WINDOW_MS)) {
        uint8_t result = clickCount;
        clickCount = 0;
        return result;
    }

    return 0;
}

// ============================================================================
// enterConfigMode() — Stop ESP-NOW, start BLE, show config screen
// ============================================================================

static void enterConfigMode() {
    Serial.println("[MAIN] Entering CONFIG MODE...");

    configModeActive = true;

    // Stop current operations
    if (currentState == STATE_RECORDING) {
        gestureEngine.stopRecording();
    }
    if (currentState == STATE_TRAINING) {
        // Sprint C2: ConfigMode override — nao voltar pro menu local.
        // ConfigMode e BLE; menu local nao coexiste com BLE ativo.
        trainingReturnToMenu = false;
        cancelTrainingMode();
    }
    // Sprint C-audit H2 (2026-05-02): defensiva — se menu estiver aberto
    // por algum caminho futuro que entre em config mode sem fechar antes,
    // garante que menuUI fica consistente.
    if (menuUI.isOpen()) {
        menuUI.exit();
    }
    audioPlayer.stop();

    // FIX #9: Reset timer do config mode timeout
    configModeStartMs = millis();

    // FIX BLE-CRITICAL: Desligar ESP-NOW antes de iniciar BLE.
    // ESP-NOW e BLE compartilham o radio WiFi do ESP32. Se ambos
    // rodam simultaneamente, callbacks ESP-NOW (heartbeat, IMU do Sensor B)
    // disparam durante config mode, causando interferencia e instabilidade.
    esp_now_deinit();
    Serial.println("[MAIN] ESP-NOW desligado para BLE");

    // Start BLE configuration server
    bleConfig.begin(voiceManager.getDeviceName());
    bleConfig.setCommandCallback(onBLECommand);

    // Show config mode on display
    // Tela de config mode — fundo azul, texto claro
    StickCP2.Display.fillScreen(TFT_NAVY);
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_NAVY);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("CONFIG", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 15);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString("Bluetooth ativo", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15);

    // Confirmation beep
    audioPlayer.beepConfirm();

    Serial.println("[MAIN] CONFIG MODE active. BLE advertising started.");
    Serial.printf("[MAIN] Device name: %s\n", voiceManager.getDeviceName());
}

// ============================================================================
// exitConfigMode() — Stop BLE, restart ESP-NOW, return to normal operation
// ============================================================================

static void exitConfigMode() {
    Serial.println("[MAIN] Exiting CONFIG MODE...");

    configModeActive = false;

    // FIX BLE-HIGH: Abortar upload em andamento antes de desligar BLE.
    // Se o cliente BLE desconectou durante upload, o arquivo SPIFFS
    // fica aberto (leak de file descriptor) e dados corrompidos.
    configHandler.abortUploadIfActive();

    // Stop BLE
    bleConfig.stop();

    // Reinitialize ESP-NOW with error checking (R2-7)
    espnow_init();
    if (!esp_now_is_peer_exist(broadcast_mac)) {
        Serial.println("[MAIN] ERROR: ESP-NOW reinit failed!");
        displayUI.showToast("Erro: ESP-NOW falhou", 3000);
        audioPlayer.beepError();
    } else {
        Serial.println("[MAIN] ESP-NOW reinitialized");
    }

    // Reload gestures and profiles (may have changed during config)
    gestureEngine.loadAllCategories();
    loadActiveProfileGestures();

    // R2-5: Verify gesture DB is not empty after config mode reload
    if (!gestureEngine.isReady() || gestureEngine.getGestureCount() == 0) {
        Serial.println("[MAIN] WARNING: Gesture reload failed, retrying...");
        // Retry once
        gestureEngine.begin();
        loadActiveProfileGestures();
        if (!gestureEngine.isReady()) {
            Serial.println("[MAIN] CRITICAL: Gesture DB unavailable after config exit");
            displayUI.showToast("Erro: gestos indisponiveis", 5000);
        }
    }

    // Update audio voice path (may have changed)
    audioPlayer.setVoicePath(voiceManager.getVoicePath());

    // Fix UX2: Update voice indicator (may have changed during config)
    displayUI.setVoiceIndicator(voiceManager.getCurrentVoice());

    // Fix UX12: Re-apply silent error mode (may have changed during config)
    audioPlayer.setSilentErrors(voiceManager.getSilentErrors());

    // Re-apply gesture level config (may have changed during config)
    gestureEngine.setLevelConfig(voiceManager.getLevelConfig());
    currentLevel = voiceManager.getGestureLevel();

    // Return to pairing state to reconnect with Sensor B
    currentState = STATE_PAIRING;
    systemActive = false;
    lastKnownConnectionState = false;
    displayUI.showPairing();

    audioPlayer.beepConfirm();

    Serial.println("[MAIN] CONFIG MODE exited. Waiting for Sensor B...");
}

// ============================================================================
// setup()
// ============================================================================

void setup() {
    // FIX INSTALL-12: Inicializar NVS ANTES do M5 — a lib M5Unified
    // usa NVS para armazenar board type. Sem NVS, board detection falha
    // e os botoes nao sao inicializados (crash LoadProhibited).
    nvs_flash_init();

    // Initialize M5StickC Plus2 hardware
    auto cfg = M5.config();
    cfg.clear_display = true;
    cfg.output_power = true;   // FIX AUDIO-01: Habilitar 5V no header para o MAX98357.
                               // Sem isso, o amplificador opera em brownout (distorcao grave).
    cfg.internal_imu = true;   // FIX INSTALL-15: Habilitar IMU explicitamente
    cfg.external_speaker.hat_spk2 = true;  // FIX INSTALL-13: HAT-SPK2
    cfg.internal_spk = false;  // Desabilitar buzzer interno — so HAT-SPK2
    StickCP2.begin(cfg);

    // FIX BTN-02: Prevenir chattering no GPIO39 (BtnB).
    // Errata ESP32: GPIO36 e GPIO39 sofrem pulsos fantasma quando o ADC
    // do modulo WiFi (SAR ADC2) alterna entre ligado/desligado.
    // adc_power_acquire() mantem o ADC ligado permanentemente, evitando glitches.
    // Sem isso, o debounce da M5Unified rejeita clicks reais no BtnB.
    // Ref: ESP32 ECO and target errata, documentacao_m5.md secao 7.
    adc_power_acquire();

    // Aguardar inicializacao completa do hardware
    delay(100);

    // Configurar speaker para qualidade maxima
    // HAT-SPK2 suporta 8k-96kHz. Saida a 48kHz para melhor qualidade.
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = 48000;   // Taxa de saida do I2S (48kHz)
    spk_cfg.dma_buf_len = 512;     // Buffer DMA maior = menos glitches
    spk_cfg.dma_buf_count = 8;
    M5.Speaker.config(spk_cfg);
    // FIX AUDIO-02: Volume reduzido para ~60% para compensar ganho analogico
    // de +9dB do MAX98357 (pino GAIN flutuante). Com 200/255 (~78%), o speaker
    // de 1W satura nos picos e distorce. 150/255 (~59%) mantem headroom.
    // BT nao e afetado (volume controlado pela propria caixa).
    M5.Speaker.setVolume(217);
    M5.Speaker.setAllChannelVolume(217);
    M5.Speaker.tone(1000, 200);
    delay(300);
    // StickCP2.update(); // FIX INSTALL-11: desabilitado — crash nos botoes

    // Set display to horizontal orientation (240x135)
    StickCP2.Display.setRotation(1);

    // Serial for debug output
    Serial.begin(115200);
    Serial.println("[MAIN] GESTUUM Sensor A starting...");

    // FIX INSTALL-15: Diagnostico IMU
    Serial.printf("[IMU] M5.Imu.isEnabled() = %d\n", M5.Imu.isEnabled());
    Serial.printf("[IMU] M5.In_I2C.isEnabled() = %d\n", M5.In_I2C.isEnabled());
    Serial.printf("[IMU] Board = %d\n", M5.getBoard());
    // Teste de leitura direta
    float tx=0, ty=0, tz=0;
    M5.Imu.getAccelData(&tx, &ty, &tz);
    Serial.printf("[IMU] Raw read: ax=%.3f ay=%.3f az=%.3f\n", tx, ty, tz);

    // Initialize display and show pairing screen
    displayUI.begin();
    displayUI.showPairing();

    // Caminho C (2026-05-02): inicializa menu local
    menuUI.begin();

    // Initialize ESP-NOW communication
    espnow_init();
    Serial.println("[MAIN] ESP-NOW initialized");

    // Initialize voice manager (loads saved voice/profile preferences from NVS)
    voiceManager.begin();
    Serial.println("[MAIN] VoiceManager initialized");

    // Initialize Bluetooth A2DP (audio para caixa de som externa)
    if (btAudio.begin("GESTUUM")) {
        Serial.println("[MAIN] BTAudio initialized");
        btAudio.connectToSaved();  // Tenta reconectar ao ultimo speaker
    } else {
        Serial.println("[MAIN] BTAudio init failed (fallback: HAT-SPK2 only)");
    }

    // Initialize audio player (SPIFFS + M5.Speaker + BT)
    if (audioPlayer.begin(&btAudio)) {
        Serial.println("[MAIN] AudioPlayer initialized");
    } else {
        Serial.println("[MAIN] AudioPlayer init FAILED");
    }

    // Set audio voice path based on saved preferences
    audioPlayer.setVoicePath(voiceManager.getVoicePath());

    // Fix UX2: Set voice indicator on display header
    displayUI.setVoiceIndicator(voiceManager.getCurrentVoice());

    // Fix UX12: Apply silent error mode from saved preferences
    audioPlayer.setSilentErrors(voiceManager.getSilentErrors());

    // Sprint C3d (Caminho C, 2026-05-02): carregar modo silencioso global
    // do NVS. Namespace separado pra nao colidir com outros configs.
    {
        Preferences prefs;
        if (prefs.begin("gestuum_ux", true)) {  // read-only
            bool savedSilentAll = prefs.getBool("silent_all", false);
            audioPlayer.setSilentAll(savedSilentAll);
            Serial.printf("[MAIN] Modo silencioso global (NVS): %s\n",
                          savedSilentAll ? "ON" : "OFF");
            prefs.end();
        }
    }

    // Initialize gesture recognition engine
    gestureEngine.begin();

    // Apply gesture level config from saved preferences
    gestureEngine.setLevelConfig(voiceManager.getLevelConfig());
    currentLevel = voiceManager.getGestureLevel();
    Serial.printf("[MAIN] Gesture level: %d\n", static_cast<int>(currentLevel));

    // Fix M4: Verify gesture engine is ready after initialization
    if (gestureEngine.isReady()) {
        Serial.println("[MAIN] GestureEngine initialized");
    } else {
        Serial.println("[MAIN] GestureEngine init FAILED — gestures unavailable");
        displayUI.showError("Gesture DB Error");
        audioPlayer.beepError();
    }

    // Load active profile gestures (appended after base category)
    loadActiveProfileGestures();
    Serial.printf("[MAIN] Loaded %d active profiles\n", activeProfileCount);

    // Initialize category manager with change callback
    categoryManager.begin();
    categoryManager.setOnChangeCallback(onCategoryChanged);
    Serial.println("[MAIN] CategoryManager initialized");

    // Initialize local IMU reader
    imuReader.begin();
    Serial.println("[MAIN] IMUReader initialized");

    // Initialize serial configuration handler (always active)
    serialConfig.begin();
    serialConfig.setCommandCallback(onSerialCommand);
    Serial.println("[MAIN] SerialConfig initialized");

    // Initialize central config command handler
    configHandler.begin(&voiceManager, &gestureEngine, &audioPlayer, &btAudio);
    configHandler.setTrainingStartCallback(onTrainingStartCmd);
    configHandler.setTrainingCancelCallback(onTrainingCancelCmd);
    Serial.println("[MAIN] ConfigHandler initialized");

    // Initialize timing references
    lastHeartbeatSent = millis();
    lastIMURead = millis();

    // Initialize connection tracking (B9)
    lastKnownConnectionState = false;

    // Enter pairing state — wait for Sensor B
    currentState = STATE_PAIRING;
    systemActive = false;

    Serial.println("[MAIN] Setup complete. Waiting for Sensor B...");
}

// ============================================================================
// loop()
// ============================================================================

void loop() {
    // FIX INSTALL-11: Usar M5.update() em vez de StickCP2.update().
    // M5.BtnA/BtnB sao referencias inicializadas no construtor global
    // de StickCP2, que pode executar ANTES do construtor de M5 (Static
    // Initialization Order Fiasco). M5.BtnA acessa o objeto real diretamente.
    M5.update();

    // Always process USB Serial commands (active in all modes)
    serialConfig.update();

    // Check for config mode activation (A+B held 3 seconds)
    detectConfigMode();

    // Caminho C / UX-v2 (2026-05-03):
    //   3x BtnB -> abre menu (se IDLE) / sai (se ja MENU) — toggle
    //   1x BtnB -> proximo item (carrossel circular)
    //   2x BtnB -> item anterior (carrossel circular) — NOVO em v2
    //   Hold BtnB 1s -> volta 1 nivel — NOVO em v2 (substitui o 2x antigo)
    //   1x BtnA  -> confirma/entra (em handleButtons quando MENU)
    if (!configModeActive) {
        uint8_t pat = detectClickPatternB();
        if (pat > 0) {
            if (menuUI.isOpen()) {
                if (pat == 3) {
                    menuUI.exit();
                    currentState = STATE_IDLE;
                    int battery = M5.Power.getBatteryLevel();
                    displayUI.showIdle(categoryManager.getCurrentCategory(),
                                       espnow_is_sensor_b_connected(), battery);
                } else if (pat == 2) {
                    menuUI.onNavigatePrev();
                } else if (pat == 1) {
                    menuUI.onNavigateNext();
                }
            } else {
                // Menu fechado: so 3x abre. 1x/2x ignorados pra nao
                // conflitar com gestos/cliques acidentais.
                if (pat == 3) {
                    if (currentState == STATE_IDLE) {
                        menuUI.enter();
                        currentState = STATE_MENU;
                    } else {
                        // Sprint C-audit A19 (2026-05-02): UX — usuario
                        // pediu menu mas estado nao permite. Feedback
                        // visivel pra nao parecer que o botao quebrou.
                        displayUI.showToast("Aguarde", 800);
                    }
                }
            }
        }
    }

    // Sprint C2 — consumir gesto pendente disparado pelo SUB_TRAIN_LIST.
    // Sprint C-audit C1/A03 (2026-05-02): so seta trainingReturnToMenu se
    // startTrainingMode realmente entrou em STATE_TRAINING. Antes, se
    // Sensor B desconectado, startTrainingMode retornava sem mudar state
    // e a flag ficava armada como zumbi — proximo cancelTrainingMode
    // (vindo de outro fluxo) abria o menu indevidamente.
    {
        char pendId[16];
        char pendName[32];
        if (menuUI.consumePendingTrain(pendId, sizeof(pendId), pendName, sizeof(pendName))) {
            currentState = STATE_IDLE;
            startTrainingMode(pendId, pendName);
            if (currentState == STATE_TRAINING) {
                trainingReturnToMenu = true;
            } else {
                // startTrainingMode falhou (ex: Sensor B desc.). Reabrir o
                // menu pra UX continua — usuario nao perde o lugar.
                menuUI.reopenAtTrainGesture();
                currentState = STATE_MENU;
            }
        }
    }

    // Sprint C3d — toggle modo silencioso pelo menu local
    if (menuUI.consumePendingSilentToggle()) {
        bool newState = !audioPlayer.isSilentAll();
        audioPlayer.setSilentAll(newState);
        // Persiste em NVS
        Preferences prefs;
        if (prefs.begin("gestuum_ux", false)) {  // read-write
            prefs.putBool("silent_all", newState);
            prefs.end();
            Serial.printf("[MAIN] silent_all -> %s (persistido)\n",
                          newState ? "ON" : "OFF");
        } else {
            Serial.println("[MAIN] FALHA persistir silent_all em NVS");
        }
        // Re-abre tela pra mostrar novo estado
        menuUI.reopenAtSilentScreen();
        currentState = STATE_MENU;
    }

    // Sprint C3b — usuario pediu pra ativar Bluetooth no menu.
    if (menuUI.consumePendingBluetoothActivate()) {
        // enterConfigMode() ja desliga ESP-NOW, liga BLE, mostra tela de config.
        // Sai do menu primeiro pra evitar conflito de estado.
        currentState = STATE_IDLE;
        Serial.println("[MAIN] BLE solicitado pelo menu local — entrando em config mode");
        enterConfigMode();
    }

    // Sprint C3a — consumir gesto pendente disparado pelo SUB_DELETE_LIST.
    // Apaga treino, recarrega gestos (hot-reload) e reabre o menu na mesma
    // posicao pra UX continua. resetGestureTraining ja existe no loader e
    // limpa trajetorias + assinaturas + flag trained=false no JSON.
    {
        char delId[16];
        char delName[32];
        if (menuUI.consumePendingDelete(delId, sizeof(delId), delName, sizeof(delName))) {
            bool ok = gestureEngine.getLoader().resetGestureTraining(delId);
            // Sprint C-audit H1 (2026-05-02): hot-reload SEMPRE, mesmo se
            // delete falhou. Sem isso, a lista em RAM podia divergir do disk
            // (gesto ainda mostrado [T] embora SPIFFS ja tenha mudado, ou
            // vice-versa) — proxima tentativa de treino usaria RAM antiga.
            gestureEngine.loadAllCategories();
            loadActiveProfileGestures();
            if (ok) {
                Serial.printf("[MAIN] Gesto apagado: %s (%s) — hot-reload OK\n",
                              delId, delName);
                audioPlayer.beepConfirm();
                displayUI.showToast("Apagado", 1200);
            } else {
                Serial.printf("[MAIN] FALHA ao apagar %s (RAM realinhada com disk)\n", delId);
                audioPlayer.beepError();
                displayUI.showToast("Falha ao apagar", 1500);
            }
            // Reabre o menu na lista de delete pra UX continua
            menuUI.reopenAtDeleteGesture();
            currentState = STATE_MENU;
        }
    }

    // If in config mode, only handle BLE and serial — skip normal operation
    if (configModeActive) {
        bleConfig.update();

        // FIX P7: Exigir hold de 1 segundo no Button B para sair do config mode.
        // Antes: single-click saia imediatamente — facil de sair acidentalmente.
        // Agora: precisa segurar por 1s, consistente com a UX de "acao intencional".
        if (M5.BtnB.pressedFor(1000)) {
            exitConfigMode();
        }

        // FIX BLE-HIGH: Timeout de inatividade — 5 minutos sem conexao BLE
        unsigned long configElapsed = millis() - configModeStartMs;
        if (configElapsed > 300000 && !bleConfig.isClientConnected()) {
            // 5 minutos sem ninguem conectado — sai automaticamente
            Serial.println("[MAIN] Config mode timeout (5min sem conexao). Auto-exit.");
            configModeStartMs = 0;
            audioPlayer.beepError();
            exitConfigMode();
            return;
        }
        // Reset timer se alguem esta conectado (uso ativo)
        if (bleConfig.isClientConnected()) {
            configModeStartMs = millis();
        }

        // Processar sequencia de audio e beeps (non-blocking)
        audioPlayer.update();

        // Update display animations
        displayUI.update();

        return;  // Skip normal state machine processing
    }

    // === Normal operation mode ===

    // Fix B9: Track connection state changes and force header update on any screen
    bool currentConnState = espnow_is_sensor_b_connected();
    if (currentConnState != lastKnownConnectionState) {
        bool wasConnected = lastKnownConnectionState;
        lastKnownConnectionState = currentConnState;
        displayUI.forceConnectionUpdate(currentConnState);
        Serial.printf("[MAIN] Connection state changed: %s\n",
                      currentConnState ? "CONNECTED" : "DISCONNECTED");

        // Fix UX8: Warn user when Sensor B disconnects
        if (wasConnected && !currentConnState) {
            audioPlayer.beepError();  // Double beep to signal disconnect
            displayUI.showToast("Sensor B desconectado", 3000);
            Serial.println("[MAIN] UX8: Sensor B disconnect warning issued");

            // FIX ALT-05: Voltar para STATE_PAIRING ao perder Sensor B.
            // Sem isso, o sistema continuava em IDLE tentando reconhecer gestos
            // sem dados do Sensor B — gestos nunca eram reconhecidos corretamente.
            if (currentState == STATE_IDLE || currentState == STATE_RECORDING) {
                if (currentState == STATE_RECORDING) {
                    gestureEngine.stopRecording();
                }
                audioPlayer.stop();
                contextActive = false;
                currentState = STATE_PAIRING;
                Serial.println("[MAIN] ALT-05: -> STATE_PAIRING (Sensor B perdido)");
            }
        }
    }

    // Process button inputs
    handleButtons();

    // FIX BUG-11: Processar emergencia recebida do Sensor B (mao direita).
    // Antes deste fix, SOS do Sensor B era ignorado pelo Sensor A.
    if (emergencyFromSensorB && currentState != STATE_EMERGENCY) {
        emergencyFromSensorB = false;
        if (currentState == STATE_RECORDING) {
            gestureEngine.stopRecording();
        }
        // Limpar estado do gesture engine para nao afetar proximo gesto
        gestureEngine.setWaitingForSensorB(false);
        audioPlayer.stop();
        contextActive = false;
        contextName[0] = '\0';
        contextAudioFile[0] = '\0';
        postAudioCooldownStart = 0;  // Reset cooldown residual
        // Sprint C-audit C2 (2026-05-02): se estava no menu local, fechar
        // antes de entrar em EMERGENCY. Sem isso, ao sair de emergencia
        // (BtnA/B hold), volta pra IDLE mas menuUI.isOpen() permanece true
        // — proximo 3xB "fecha" o menu invisivel em vez de abrir.
        if (currentState == STATE_MENU) {
            menuUI.exit();
            trainingReturnToMenu = false;  // se vier de fluxo train→menu
        }
        currentState = STATE_EMERGENCY;
        emergencyAudioPlayed = false;
        Serial.println("[MAIN] -> EMERGENCY (recebida do Sensor B)");
    } else if (emergencyFromSensorB) {
        emergencyFromSensorB = false;  // Ja em emergencia, so limpa flag
    }

    // Read IMU at 50Hz and feed to gesture engine
    handleIMU();

    // Send heartbeat to Sensor B at 1Hz
    handleHeartbeat();

    // Fix UX9: Check battery level and emit low-battery alerts
    handleBatteryAlert();

    // Check automation gestures EVERY iteration (independent of state)
    // FIX INSTALL-16: Desabilitar durante training para nao interferir
    // Sprint C6 (2026-05-02): tambem desabilita durante MENU local pra
    // gesto acidental enquanto navega nao disparar comando de automacao.
    if (!trainingActive && currentState != STATE_MENU) {
        handleAutomationGestures();
    }

    // FIX CRITICAL #2: Processar pairing diferido do callback ESP-NOW.
    // Sem esta chamada, Sensor B nunca era pareado e o sistema ficava
    // permanentemente em STATE_PAIRING — completamente nao-funcional.
    espnow_processPairing();

    // Execute current state logic
    handleState();

    // Processar sequencia de audio e beeps (non-blocking)
    // update() detecta fim de arquivo via M5.Speaker.isPlaying(),
    // processa gap entre file1→file2 na frase composta,
    // e roda a maquina de estados do beep de erro.
    audioPlayer.update();
    btAudio.update();

    // Update display animations
    displayUI.update();
}

// ============================================================================
// handleButtons()
// ============================================================================

/**
 * Process button events:
 * - Button A click: cycle to previous category (UX6: bidirectional navigation)
 * - Button B click: cycle to next category
 * - Button A hold 2s: toggle emergency mode
 * - Button B hold 2s in emergency: exit to IDLE (UX7: prevents accidental cancel)
 */
static void handleButtons() {
    // Caminho C / UX-v2: quando menu local aberto:
    //   1xA = confirma/entra
    //   Hold B 1s = volta 1 nivel (NOVO em v2)
    // BtnB clicks (1x/2x/3x) tratados em detectClickPatternB() no loop().
    if (currentState == STATE_MENU) {
        if (M5.BtnA.wasClicked()) {
            menuUI.onConfirm();
        }
        // UX-v2: hold B 1s -> back-to-parent. Usa wasReleaseFor pra
        // disparar SO no release apos manter pressionado >=1000ms.
        // Evita conflito com 2xB rapido (que tem janela 350ms total).
        if (M5.BtnB.wasReleaseFor(1000)) {
            menuUI.onBackToParent();
        }
        return;
    }

    // During training, button handling is done inside handleTrainingState()
    // Only allow BtnA hold for emergency during training
    if (currentState == STATE_TRAINING) {
        if (M5.BtnA.wasHold()) {
            // Sprint C5 (Caminho C, 2026-05-02): SOS local removido do
            // Sensor A. BtnA hold em training agora SO cancela training
            // (volta pra IDLE/menu conforme origem). Emergencia so vem
            // via ESP-NOW do Sensor B (mao direita = mao do SOS).
            cancelTrainingMode();
            Serial.println("[MAIN] Training cancelled (BtnA hold — sem emergencia local)");
        }
        // BtnB click is handled inside handleTrainingState() to cancel training
        return;
    }

    // Categorias removidas do MVP — todos os gestos carregados de uma vez.
    // BtnB triple-click entra em config mode (detectado antes de handleButtons).
    // BtnA click livre para uso futuro.

    // UX7: Button B hold 2s: exit emergency mode (prevents accidental cancel by tremor/panic)
    if (M5.BtnB.wasHold()) {
        if (currentState == STATE_EMERGENCY) {
            currentState = STATE_IDLE;
            emergencyAudioPlayed = false;
            audioPlayer.stop();
            // Cancel SOS via broadcast (atuador externo se presente).
            // Nota 2026-05-02: AtomS3 LED original foi removido.
            // CMD_LIGHT_OFF preservado pra v2 (atuadores domoticos).
            espnow_send_automation(CMD_LIGHT_OFF, 0);
            int battery = M5.Power.getBatteryLevel();
            displayUI.showIdle(categoryManager.getCurrentCategory(),
                               espnow_is_sensor_b_connected(), battery);
            Serial.println("[MAIN] EMERGENCY -> IDLE (BtnB hold 2s)");
        }
    }

    // Sprint C5 (Caminho C, 2026-05-02): BtnA hold no Sensor A nao
    // ENTRA mais em emergencia. SOS local removido — emergencia so vem
    // via ESP-NOW do Sensor B (mao direita = unica que dispara SOS).
    // BtnA hold ainda permite SAIR de emergencia (acessibilidade — user
    // pode usar qualquer botao pra cancelar SOS recebido falso positivo).
    if (M5.BtnA.wasHold() && currentState == STATE_EMERGENCY) {
        currentState = STATE_IDLE;
        emergencyAudioPlayed = false;
        audioPlayer.stop();
        // FIX CRITICAL #4: Cancel SOS na fita LED
        espnow_send_automation(CMD_LIGHT_OFF, 0);
        int battery = M5.Power.getBatteryLevel();
        displayUI.showIdle(categoryManager.getCurrentCategory(),
                           espnow_is_sensor_b_connected(), battery);
        Serial.println("[MAIN] EMERGENCY -> IDLE (BtnA hold — saida)");
    }
}

// ============================================================================
// handleIMU()
// ============================================================================

/**
 * Read local IMU at 50Hz (every IMU_SAMPLE_PERIOD_MS = 20ms).
 * Feed acceleration data to gesture engine and category manager.
 * Also consume any remote IMU data from Sensor B via ESP-NOW.
 *
 * Fix C1: Use espnow_getIMUData() to safely copy from double-buffer.
 * Fix H3: Validate acceleration values before feeding to gesture engine.
 */
static void handleIMU() {
    unsigned long now = millis();
    if (now - lastIMURead < IMU_SAMPLE_PERIOD_MS) {
        return;
    }
    lastIMURead = now;

    // Sprint C-audit A01 (2026-05-02): em STATE_MENU, nao alimentar
    // o gesture engine. Movimentos do pulso navegando o menu sao apenas
    // ruido — nao precisam acumular trajetoria/orbital. Continua lendo
    // IMU local pra manter timestamps coerentes? Nao precisa — skip total.
    if (currentState == STATE_MENU) {
        return;
    }

    // Read and filter local IMU
    imuReader.update();

    float ax = imuReader.getAccelX();
    float ay = imuReader.getAccelY();
    float az = imuReader.getAccelZ();

    // Fix H3: Validate local acceleration before feeding to gesture engine
    if (isAccelValid(ax, ay, az)) {
        // FIX TRAIN-SENSOR: Filtrar sensor residual durante RECONHECIMENTO E TREINO.
        // Regra: leitura de dispositivo e unica — ou Sensor A, ou Sensor B, nunca ambos.
        // Reconhecimento: waitingForSensorB controla qual sensor alimentar.
        // Treino: se treinando OBJETO (Gxx), NAO alimentar Sensor A (so B).
        //         se treinando CONTEXTO (CXxx), alimentar Sensor A normalmente.
        bool isTrainingContextA = (currentState == STATE_TRAINING) &&
            (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X');
        bool isTrainingObjectA = (currentState == STATE_TRAINING) && !isTrainingContextA;
        bool feedSensorA;
        if (isTrainingObjectA) {
            feedSensorA = false;  // Treino de objeto: so Sensor B
        } else if (isTrainingContextA) {
            feedSensorA = true;   // Treino de contexto: so Sensor A
        } else {
            // Reconhecimento: alimentar A quando NAO esperando sensor B
            feedSensorA = !gestureEngine._waitingForSensorB;
        }

        if (feedSensorA) {
            gestureEngine.updateSensorA(ax, ay, az);
            float gx = imuReader.getGyroX();
            float gy = imuReader.getGyroY();
            float gz = imuReader.getGyroZ();
            gestureEngine.updateSensorAGyro(gx, gy, gz);
        }

        categoryManager.update(ax, ay, az);
    } else {
        Serial.println("[MAIN] WARNING: Local IMU data out of range, skipping");
    }

    // Fix C1: Consume remote IMU data from Sensor B using safe double-buffer copy
    IMUPacket sensorBData;
    if (espnow_getIMUData(&sensorBData)) {
        // FIX A↔B #7: Detectar packet loss via sequence number
        static uint16_t lastSeq = 0;
        static bool seqInitialized = false;
        static uint32_t totalDropped = 0;

        if (seqInitialized) {
            uint16_t expected = lastSeq + 1;
            if (sensorBData.seq != expected) {
                // Calcular quantos pacotes foram perdidos (considerando wrap uint16_t)
                uint16_t gap = sensorBData.seq - expected;  // funciona com unsigned wrap
                if (gap < 1000) {  // Sanity check — gap > 1000 provavelmente e reboot do Sensor B
                    totalDropped += gap;
                    DBG2("[MAIN] Packet loss: %d dropped (seq %u→%u, total: %lu)\n",
                                  gap, lastSeq, sensorBData.seq, totalDropped);
                } else {
                    // Sensor B reiniciou — reset do contador
                    totalDropped = 0;
                    Serial.printf("[MAIN] Sensor B seq reset detected (seq %u)\n", sensorBData.seq);
                }
            }
        }
        lastSeq = sensorBData.seq;
        seqInitialized = true;

        // Convert from int16_t scaled values back to float (accel was * 1000)
        float bx = sensorBData.ax / 1000.0f;
        float by = sensorBData.ay / 1000.0f;
        float bz = sensorBData.az / 1000.0f;

        // Fix H3: Validate remote acceleration before feeding to gesture engine
        if (isAccelValid(bx, by, bz)) {
            // FIX SERIAL-04: Log Sensor B so em modo verbose
#if DEBUG_LEVEL >= 2
            static uint32_t dbgCnt = 0;
            if (++dbgCnt % 50 == 0) {
                Serial.printf("[DBG-B] bx=%.2f by=%.2f bz=%.2f\n", bx, by, bz);
            }
#endif
            // FIX TRAIN-SENSOR: Filtrar sensor residual durante RECONHECIMENTO E TREINO.
            // Regra: leitura de dispositivo e unica — ou Sensor A, ou Sensor B, nunca ambos.
            // Reconhecimento: se gravando contexto (mao esquerda), NAO alimentar Sensor B.
            // Treino: se treinando CONTEXTO (CXxx), NAO alimentar Sensor B (so A).
            //         se treinando OBJETO (Gxx), alimentar Sensor B normalmente.
            bool isTrainingContext = (currentState == STATE_TRAINING) &&
                (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X');
            bool isTrainingObject = (currentState == STATE_TRAINING) && !isTrainingContext;
            bool feedSensorB;
            if (isTrainingContext) {
                feedSensorB = false;  // Treino de contexto: so Sensor A
            } else if (isTrainingObject) {
                feedSensorB = true;   // Treino de objeto: so Sensor B
            } else {
                // Reconhecimento: alimentar B quando nao gravando OU esperando sensor B
                feedSensorB = !gestureEngine.isRecording() || gestureEngine._waitingForSensorB;
            }

            if (feedSensorB) {
                gestureEngine.updateSensorB(bx, by, bz);
                float bgx = sensorBData.gx / 100.0f;
                float bgy = sensorBData.gy / 100.0f;
                float bgz = sensorBData.gz / 100.0f;
                gestureEngine.updateSensorBGyro(bgx, bgy, bgz);
            }
        } else {
            Serial.println("[MAIN] WARNING: Sensor B IMU data out of range, skipping");
        }
    }
}

// ============================================================================
// handleHeartbeat()
// ============================================================================

/**
 * Send heartbeat broadcast every HEARTBEAT_INTERVAL_MS (1000ms).
 * Keeps Sensor B pairing alive and provides battery level.
 */
static void handleHeartbeat() {
    unsigned long now = millis();
    if (now - lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS) {
        espnow_send_heartbeat();
        lastHeartbeatSent = now;
    }
}

// ============================================================================
// handleBatteryAlert() — Fix UX9: Audible low battery warning
// ============================================================================

/**
 * Check battery level every 10 seconds and emit periodic beeps when low.
 * - Below 15%: beep every 60 seconds
 * - Below 5%:  beep every 30 seconds
 *
 * Uses static timing variables to avoid checking/alerting every loop iteration.
 */
static void handleBatteryAlert() {
    static unsigned long lastBatteryCheckTime = 0;
    static unsigned long lastBatteryAlertTime = 0;

    unsigned long now = millis();

    // Only check battery every 10 seconds
    if (now - lastBatteryCheckTime < 10000) {
        return;
    }
    lastBatteryCheckTime = now;

    int battery = M5.Power.getBatteryLevel();

    if (battery < 5) {
        // Critical: beep every 30 seconds
        if (now - lastBatteryAlertTime >= 30000) {
            lastBatteryAlertTime = now;
            audioPlayer.beepError();
            Serial.printf("[MAIN] UX9: Battery critical (%d%%), alert issued\n", battery);
        }
    } else if (battery < 15) {
        // Low: beep every 60 seconds
        if (now - lastBatteryAlertTime >= 60000) {
            lastBatteryAlertTime = now;
            audioPlayer.beepConfirm();  // Softer tone for low (not critical)
            Serial.printf("[MAIN] UX9: Battery low (%d%%), alert issued\n", battery);
        }
    }
}

// ============================================================================
// handleState()
// ============================================================================

/**
 * Main state machine logic. Each state checks conditions and transitions
 * to the appropriate next state.
 */
static void handleState() {
    switch (currentState) {

    // -- PAIRING: wait for Sensor B connection --
    case STATE_PAIRING: {
        if (espnow_is_sensor_b_connected()) {
            // Sensor B discovered and heartbeat received -> transition to IDLE
            currentState = STATE_IDLE;
            systemActive = true;
            lastKnownConnectionState = true;
            int battery = M5.Power.getBatteryLevel();
            displayUI.showIdle(categoryManager.getCurrentCategory(), true, battery);
            audioPlayer.beepConfirm();
            Serial.println("[MAIN] PAIRING -> IDLE (Sensor B connected)");
        }
        break;
    }

    // -- IDLE: ready for gesture activation via double tap --
    case STATE_IDLE: {
        if (detectDoubleTap()) {
            // Push-to-talk activated -> start recording + ativar modo conversa
            currentState = STATE_RECORDING;
            gestureEngine.startRecording();
            // UX: Tela "Gesto 01" — barra azul no topo, fundo preto
            displayUI.suppressUpdates();  // Evita update() sobrescrever com "LENDO..."
            StickCP2.Display.fillScreen(TFT_BLACK);
            StickCP2.Display.fillRect(0, 0, SCREEN_WIDTH, 20, 0x04BF);  // Barra azul topo
            StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            StickCP2.Display.setTextDatum(ML_DATUM);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.drawString("Gesto", 10, SCREEN_HEIGHT / 2);
            StickCP2.Display.setTextDatum(MR_DATUM);
            StickCP2.Display.setTextSize(4);
            StickCP2.Display.drawString("01", SCREEN_WIDTH - 10, SCREEN_HEIGHT / 2);
            // Barra de progresso fina na parte inferior
            StickCP2.Display.drawRect(20, SCREEN_HEIGHT - 12, SCREEN_WIDTH - 40, 6, TFT_DARKGREY);
            recordStartTime = millis();
            audioPlayer.beepConfirm();
            Serial.println("[MAIN] IDLE -> RECORDING (double tap, gesto 01)");
        }
        break;
    }

    // -- RECORDING: capturing gesture data from both sensors --
    case STATE_RECORDING: {
        unsigned long elapsed = millis() - recordStartTime;

        // UX: Atualizar barra de progresso fina (preenche com o tempo)
        {
            // Usar minRecordingMs como referencia (2s) — nao o timeout (5s)
            // Assim a barra preenche 100% quando o gesto pode completar
            unsigned long barRef = voiceManager.getLevelConfig().minRecordingMs;
            if (barRef == 0) barRef = 2000;
            int barWidth = SCREEN_WIDTH - 42;  // Largura interna
            int filled = (int)((elapsed * barWidth) / barRef);
            if (filled > barWidth) filled = barWidth;
            uint16_t barColor = gestureEngine._waitingForSensorB ? TFT_YELLOW : 0x04BF;
            StickCP2.Display.fillRect(21, SCREEN_HEIGHT - 11, filled, 4, barColor);
        }

        // Check timeout: if recording exceeds level's gestureTimeoutMs
        if (elapsed > voiceManager.getLevelConfig().gestureTimeoutMs) {
            gestureEngine.stopRecording();

            currentState = STATE_IDLE;
            int battery = M5.Power.getBatteryLevel();
            displayUI.showIdle(categoryManager.getCurrentCategory(),
                               espnow_is_sensor_b_connected(), battery);
            audioPlayer.beepError();
            Serial.println("[MAIN] RECORDING -> IDLE (timeout)");
            break;
        }

        // Fix B4/B5: Check if gesture is naturally complete (motion stopped)
        // instead of always waiting for the full 3s timeout.
        // isGestureComplete() returns true when:
        //   1. elapsed >= MIN_RECORDING_DURATION_MS (800ms)
        //   2. sufficient trajectory points recorded
        //   3. user stopped moving for 300ms (hand returned to rest)
        if (gestureEngine.isGestureComplete()) {
            gestureEngine.stopRecording();
            displayUI.showProcessing();
            currentState = STATE_MATCHING;
            Serial.printf("[MAIN] RECORDING -> MATCHING (gesture complete, %lums)\n", elapsed);

            // Dump trajetoria capturada para analise
            const auto& capturedA = gestureEngine.getMatrixA().getTrajectory();
            Serial.printf("[CAPTURED_A] %d pts, mov=%.1f:\n",
                          (int)capturedA.size(), gestureEngine.getMatrixA().getTotalMovement());
            for (size_t i = 0; i < capturedA.size(); i++) {
                Serial.printf("  [%d] x=%d y=%d z=%d\n",
                              (int)i, capturedA[i].x, capturedA[i].y, capturedA[i].z);
            }
        }
        break;
    }

    // -- MATCHING: process recorded gesture via DTW --
    case STATE_MATCHING: {
        // FIX FLOW-04: Resetar flag apos captura do gesto
        gestureEngine.setWaitingForSensorB(false);
        GestureResult result = gestureEngine.processGesture();

        if (result.matched) {

            // Check if gesture has an audio file to play
            bool hasAudio = (strlen(result.audioFile) > 0);

            // FIX A↔C #2: Removido espnow_send_automation duplicado.
            // send_gesture ja envia automation_cmd dentro do GesturePacket.
            // Nota 2026-05-02: AtomS3 LED removido — broadcast continua
            // pra Sensor B + qualquer ouvinte futuro (atuadores v2).

            // Broadcast recognized gesture (inclui automation_cmd se houver)
            espnow_send_gesture(
                static_cast<uint8_t>(categoryManager.getCurrentCategory()),
                static_cast<uint16_t>(result.gestureIndex),
                static_cast<uint8_t>(result.automationCmd),
                systemActive);

            // === Context/phrase building logic ===
            if (result.isContext) {
                // Matched a context gesture -> store context and wait for object
                strncpy(contextName, result.gestureName, sizeof(contextName) - 1);
                contextName[sizeof(contextName) - 1] = '\0';
                strncpy(contextAudioFile, result.audioFile, sizeof(contextAudioFile) - 1);
                contextAudioFile[sizeof(contextAudioFile) - 1] = '\0';
                contextActive = true;
                contextStartTime = millis();

                // FIX FLOW-10: Contexto so mostra na tela, NAO toca audio.
                // O audio so sai quando a frase completa (contexto + objeto).
                // Isso evita tocar "I want" sozinho e depois "I want water" de novo.
                // showContextWait removido — transicao imediata para Gesto 02
                currentState = STATE_CONTEXT_WAIT;

                Serial.printf("[MAIN] MATCHING -> CONTEXT_WAIT (context: %s, conf: %.2f)\n",
                              result.gestureName, result.confidence);

            } else if (contextActive && hasAudio) {
                // Object gesture matched while a context is active -> phrase building
                // Play context audio + object audio in sequence
                currentState = STATE_SPEAKING;

                // Build phrase display: "context + object"
                char phrase[64];
                snprintf(phrase, sizeof(phrase), "%s %s", contextName, result.gestureName);
                displayUI.showMatched(phrase, result.confidence);

                // FIX #2: Checar retorno — se audio falhar, nao travar em STATE_SPEAKING
                if (!audioPlayer.playSequence(contextAudioFile, result.audioFile)) {
                    displayUI.showToast("Audio indisponivel", 2000);
                    audioPlayer.beepError();
                    currentState = STATE_IDLE;
                }

                Serial.printf("[MAIN] MATCHING -> SPEAKING (phrase: %s %s, conf: %.2f)\n",
                              contextName, result.gestureName, result.confidence);

                // Clear context buffer
                contextActive = false;
                contextName[0] = '\0';
                contextAudioFile[0] = '\0';

            } else if (hasAudio) {
                // Normal object gesture (no context active) -> play audio
                currentState = STATE_SPEAKING;
                displayUI.showMatched(result.gestureName, result.confidence);

                // FIX #2: Se arquivo WAV nao existe, feedback de erro ao inves de travar
                if (!audioPlayer.playFile(result.audioFile)) {
                    displayUI.showToast("Audio indisponivel", 2000);
                    audioPlayer.beepError();
                    currentState = STATE_IDLE;
                }

                Serial.printf("[MAIN] MATCHING -> SPEAKING (matched: %s, conf: %.2f)\n",
                              result.gestureName, result.confidence);
            } else {
                // Automation-only gesture (no audio) -> go back to IDLE
                displayUI.showMatched(result.gestureName, result.confidence);
                currentState = STATE_IDLE;

                Serial.printf("[MAIN] MATCHING -> IDLE (automation-only: %s, cmd: 0x%02X)\n",
                              result.gestureName, static_cast<int>(result.automationCmd));
            }
        } else {
            // No match
            displayUI.showNoMatch();
            audioPlayer.beepError();
            currentState = STATE_IDLE;
            Serial.println("[MAIN] MATCHING -> IDLE (no match)");
        }
        break;
    }

    // -- SPEAKING: wait for audio playback to finish --
    case STATE_SPEAKING: {
        // Allow double-tap to interrupt audio and start new gesture
        if (detectDoubleTap()) {
            audioPlayer.stop();
            // Clear any active context on interrupt
            contextActive = false;
            contextName[0] = '\0';
            contextAudioFile[0] = '\0';
            gestureEngine.startRecording();
            currentState = STATE_RECORDING;
            recordStartTime = millis();
            displayUI.showRecording();
            audioPlayer.beepConfirm();
            Serial.println("[MAIN] SPEAKING -> RECORDING (double tap interrupt)");
            break;
        }

        if (!audioPlayer.isPlaying()) {
            // FIX FLOW-08: Cooldown pos-audio — speaker vibra o acelerometro.
            // Sem espera, a vibracao residual e capturada como gesto → loop infinito.
            if (postAudioCooldownStart == 0) {
                postAudioCooldownStart = millis();
                break;
            }
            if (millis() - postAudioCooldownStart < POST_AUDIO_COOLDOWN_MS) {
                break;
            }
            postAudioCooldownStart = 0;

            currentState = STATE_IDLE;
            int battery = M5.Power.getBatteryLevel();
            displayUI.showIdle(categoryManager.getCurrentCategory(),
                               espnow_is_sensor_b_connected(), battery);
            Serial.println("[MAIN] SPEAKING -> IDLE (audio done, cooldown complete)");
        }
        break;
    }

    // -- CONTEXT_WAIT: espera gesto de objeto apos contexto detectado --
    // FIX FLOW-06: Auto-inicia gravacao sem double-tap.
    // O double-tap so e necessario para iniciar a "conversa".
    // Apos o contexto, a gravacao do objeto comeca automaticamente.
    case STATE_CONTEXT_WAIT: {
        // Check timeout: se ninguem mexe por CONTEXT_TIMEOUT_MS, volta pra IDLE
        unsigned long ctxElapsed = millis() - contextStartTime;
        if (ctxElapsed > CONTEXT_TIMEOUT_MS) {
            contextActive = false;
            contextName[0] = '\0';
            contextAudioFile[0] = '\0';
            currentState = STATE_IDLE;
            int battery = M5.Power.getBatteryLevel();
            displayUI.showIdle(categoryManager.getCurrentCategory(),
                               espnow_is_sensor_b_connected(), battery);
            Serial.println("[MAIN] CONTEXT_WAIT -> IDLE (timeout)");
            break;
        }

        // Cooldown removido — FLOW-10 removeu audio do contexto,
        // entao nao ha vibracao de speaker para esperar dissipar.
        // O cooldown de 500ms era latencia desnecessaria para o usuario.
        postAudioCooldownStart = 0;

        // Auto-iniciar gravacao do objeto (Sensor B, mao direita)
        gestureEngine.startRecording();
        gestureEngine.setWaitingForSensorB(true);
        currentState = STATE_RECORDING;
        recordStartTime = millis();
        // UX: Beep + Tela "Gesto 02" — barra amarela no topo, fundo preto
        audioPlayer.beepConfirm();
        displayUI.suppressUpdates();  // Evita "LENDO..." sobrescrever
        StickCP2.Display.fillScreen(TFT_BLACK);
        StickCP2.Display.fillRect(0, 0, SCREEN_WIDTH, 20, TFT_YELLOW);  // Barra amarela topo
        StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        StickCP2.Display.setTextDatum(ML_DATUM);
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.drawString("Gesto", 10, SCREEN_HEIGHT / 2);
        StickCP2.Display.setTextDatum(MR_DATUM);
        StickCP2.Display.setTextSize(4);
        StickCP2.Display.drawString("02", SCREEN_WIDTH - 10, SCREEN_HEIGHT / 2);
        StickCP2.Display.drawRect(20, SCREEN_HEIGHT - 12, SCREEN_WIDTH - 40, 6, TFT_DARKGREY);
        // Sem beep aqui — beep sobrepoe o audio do contexto que acabou de tocar
        Serial.println("[MAIN] CONTEXT_WAIT -> RECORDING (auto, waiting SensorB for object)");
        break;
    }

    // -- EMERGENCY: alerta visual e sonoro, toca socorro.wav uma vez --
    case STATE_EMERGENCY: {
        // FIX M09: showEmergency() e audio so na entrada do estado.
        // Antes: showEmergency() era chamado a cada loop() → resetava
        // _lastToggle a cada frame, impedindo a animacao de toggle
        // vermelho/preto de progredir. Agora so chama uma vez;
        // displayUI.update() cuida da animacao de piscar.
        if (!emergencyAudioPlayed) {
            displayUI.showEmergency();
            espnow_send_emergency(3, 0);
            emergencyAudioPlayed = true;
            _emergencySosLastMs = 0;  // Forcar primeiro SOS imediato
            Serial.println("[MAIN] EMERGENCY: alert sent, SOS Morse loop ativo");
            _emergencyLastRetransmit = millis();
        }

        // Bip de emergencia em loop continuo (3 bips a cada 3s)
        if (millis() - _emergencySosLastMs >= 3000) {
            _emergencySosLastMs = millis();
            audioPlayer.playSOS();
        }

        // Retransmissao ESP-NOW periodica (a cada 2s)
        if (millis() - _emergencyLastRetransmit >= 2000) {
            _emergencyLastRetransmit = millis();
            espnow_send_emergency(3, 0);
        }
        break;
    }

    // -- TRAINING: recording gesture samples for calibration --
    case STATE_TRAINING: {
        handleTrainingState();
        break;
    }

    // -- MENU: Caminho C (2026-05-02) — menu local navegavel pelos botoes.
    // Render e idempotente (so redesenha se _dirty). Inputs vem do loop()
    // via detectClickPatternB() e do handleButtons() para BtnA confirm.
    case STATE_MENU: {
        menuUI.render();
        break;
    }

    // -- INIT: should not occur during loop, only in setup --
    case STATE_INIT:
    default:
        break;
    }
}

// ============================================================================
// detectDoubleTap()
// ============================================================================

/**
 * Detect two acceleration peaks within ACTIVATION_TIMEOUT_MS (500ms).
 * A peak is defined as total acceleration magnitude exceeding
 * DOUBLE_TAP_ACCEL_THRESHOLD (3.0g).
 *
 * Uses static variables to persist state between calls.
 *
 * Fix M2: Reset ALL static state (firstTapDetected, waitingForRelease)
 *         on successful double tap detection.
 */
static bool detectDoubleTap() {
    // Static state for double tap detection
    static bool firstTapDetected = false;
    static unsigned long firstTapTime = 0;
    static bool waitingForRelease = false;

    float ax = imuReader.getAccelX();
    float ay = imuReader.getAccelY();
    float az = imuReader.getAccelZ();

    // Calculate total acceleration magnitude
    // Use level-specific doubleTapThreshold instead of hardcoded DOUBLE_TAP_ACCEL_THRESHOLD
    float magnitude = fabsf(ax) + fabsf(ay) + fabsf(az);
    float tapThreshold = voiceManager.getLevelConfig().doubleTapThreshold;
    bool isPeak = (magnitude > tapThreshold);

    // FIX SERIAL-02 + SERIAL-04: Log de TAP so em modo verbose
#if DEBUG_LEVEL >= 2
    static unsigned long lastDebugLog = 0;
    if (millis() - lastDebugLog >= 1000) {
        lastDebugLog = millis();
        Serial.printf("[TAP] mag=%.2f thr=%.2f\n", magnitude, tapThreshold);
    }
#endif

    // UX3: Near-miss detection — flash hint quando tap e quase forte o suficiente
    // FIX L05: So mostra hint visual quando em STATE_IDLE.
    // Antes: flashTapHint() era chamado em qualquer estado (SPEAKING, CONTEXT_WAIT),
    // causando glitch visual (ponto branco na tela de resultado/contexto).
    static unsigned long lastNearMissTime = 0;
    float halfThreshold = tapThreshold * 0.5f;
    if (!isPeak && magnitude > halfThreshold && magnitude <= tapThreshold) {
        unsigned long now = millis();
        if (now - lastNearMissTime >= 500 && currentState == STATE_IDLE) {
            lastNearMissTime = now;
            displayUI.flashTapHint();
        }
    }

    // If waiting for acceleration to drop below threshold before detecting next tap
    if (waitingForRelease) {
        if (!isPeak) {
            waitingForRelease = false;
        }
        return false;
    }

    if (isPeak) {
        if (!firstTapDetected) {
            // First tap detected
            firstTapDetected = true;
            firstTapTime = millis();
            waitingForRelease = true;
        } else {
            // Check if second tap is within the activation window
            unsigned long elapsed = millis() - firstTapTime;
            if (elapsed <= ACTIVATION_TIMEOUT_MS) {
                // Fix M2: Double tap confirmed! Reset ALL state for next detection.
                firstTapDetected = false;
                firstTapTime = 0;
                waitingForRelease = false;
                return true;
            } else {
                // Too slow — treat this as a new first tap
                firstTapTime = millis();
                waitingForRelease = true;
            }
        }
    } else {
        // No peak — check if first tap has expired
        if (firstTapDetected && (millis() - firstTapTime > ACTIVATION_TIMEOUT_MS)) {
            firstTapDetected = false;
        }
    }

    return false;
}

// ============================================================================
// onCategoryChanged() — callback from CategoryManager
// ============================================================================

/**
 * Called by CategoryManager whenever the active category changes.
 * Updates the gesture database, display, and broadcasts to peers.
 */
static void onCategoryChanged(Category newCat) {
    // Reload gesture definitions for the new category
    gestureEngine.loadAllCategories();

    // Reload active profile gestures (appended after new base category)
    loadActiveProfileGestures();

    // Update display with new category
    int battery = M5.Power.getBatteryLevel();
    displayUI.showIdle(newCat, espnow_is_sensor_b_connected(), battery);

    // Broadcast category change to Sensor B (AtomS3 LED removido em 2026-05-02)
    espnow_send_category(newCat);

    Serial.printf("[MAIN] Category changed to %d\n", static_cast<int>(newCat));
}

// ============================================================================
// onTrainingStartCmd() — Callback from ConfigHandler when train_start received
// ============================================================================

static void onTrainingStartCmd(const char* gestureId, const char* gestureName) {
    startTrainingMode(gestureId, gestureName);
    // FIX ALT-17: Se o treino foi bloqueado (Sensor B desconectado),
    // resetar estado do config handler para nao ficar em "training active".
    if (currentState != STATE_TRAINING) {
        configHandler.resetTrainingState();
    }
}

// ============================================================================
// onTrainingCancelCmd() — Callback from ConfigHandler when train_cancel received
// ============================================================================

static void onTrainingCancelCmd() {
    if (currentState == STATE_TRAINING) {
        cancelTrainingMode();
    }
}

// ============================================================================
// startTrainingMode() — Enter training mode for a specific gesture
// ============================================================================

/**
 * Begin training mode: record 3 samples of a gesture to calibrate trajectories.
 * Can be triggered by config_handler (via app/serial command) or device menu.
 *
 * @param gestureId  String ID of the gesture to train (e.g., "G01", "EM03")
 * @param gestureName  Human-readable name for display (e.g., "agua")
 */
static void startTrainingMode(const char* gestureId, const char* gestureName) {
    // FIX ALT-17: Bloquear treino sem Sensor B pareado.
    // Sem dados do Sensor B, o gesto salvo tera trajectoryB vazio,
    // causando match errado no reconhecimento (signatureB invalida).
    if (!espnow_is_sensor_b_connected()) {
        Serial.println("[MAIN] ALT-17: Treino bloqueado — Sensor B nao conectado");
        displayUI.showToast("Conecte Sensor B", 3000);
        audioPlayer.beepError();
        return;
    }

    if (currentState == STATE_RECORDING) {
        gestureEngine.stopRecording();
    }
    audioPlayer.stop();

    // Store training target
    strncpy(trainingGestureId, gestureId, sizeof(trainingGestureId) - 1);
    trainingGestureId[sizeof(trainingGestureId) - 1] = '\0';
    strncpy(trainingGestureName, gestureName, sizeof(trainingGestureName) - 1);
    trainingGestureName[sizeof(trainingGestureName) - 1] = '\0';

    // Reset training state
    gestureEngine.clearTrainingSamples();
    // FIX TRAIN-SENSOR: Informar ao engine se e contexto ou objeto
    // para que addTrainingSample/computeAverageGesture usem o sensor correto.
    bool isCtx = (gestureId[0] == 'C' && gestureId[1] == 'X');
    gestureEngine.setTrainingIsContext(isCtx);
    trainingActive = true;
    trainingPhase = 0;  // WAITING_FOR_TAP
    trainingReturnState = STATE_IDLE;

    // Enter training state
    currentState = STATE_TRAINING;

    // Show initial training instructions
    int sampleNum = gestureEngine.getTrainingSampleCount() + 1;
    displayUI.showTrainingWait(trainingGestureName, sampleNum, 3);
    audioPlayer.beepConfirm();

    Serial.printf("[MAIN] -> STATE_TRAINING (gesture: %s [%s])\n",
                  trainingGestureName, trainingGestureId);
}

// ============================================================================
// cancelTrainingMode() — Cancel training and return to IDLE
// ============================================================================

static void cancelTrainingMode() {
    // FIX ALT-04: Fase 1 = COUNTDOWN, Fase 2 = RECORDING.
    // stopTrainingSample() so deve ser chamado durante gravacao real (fase 2).
    if (trainingPhase == 2) {
        // Currently recording — stop it
        gestureEngine.stopTrainingSample();
    }

    gestureEngine.clearTrainingSamples();
    gestureEngine.setTrainingIsContext(false);  // FIX TRAIN-SENSOR: reset flag
    trainingActive = false;
    trainingPhase = 0;
    trainingGestureId[0] = '\0';
    trainingGestureName[0] = '\0';

    // Reset config handler's training state too
    configHandler.resetTrainingState();

    // Sprint C2: se treino veio do menu, voltar pro menu. Senao, IDLE.
    if (trainingReturnToMenu) {
        trainingReturnToMenu = false;
        menuUI.reopenAtTrainGesture();
        currentState = STATE_MENU;
        Serial.println("[MAIN] Training cancelled — voltando pro menu (origem: menu local)");
    } else {
        currentState = STATE_IDLE;
        int battery = M5.Power.getBatteryLevel();
        displayUI.showIdle(categoryManager.getCurrentCategory(),
                           espnow_is_sensor_b_connected(), battery);
        Serial.println("[MAIN] Training cancelled by user");
    }
    audioPlayer.beepError();
}

// ============================================================================
// handleTrainingState() — Sub-state machine for training mode
// ============================================================================

/**
 * Training sub-states:
 *   trainingPhase 0: WAITING_FOR_TAP — show "Faca o gesto (N/3)", wait for double-tap
 *   trainingPhase 1: RECORDING — recording gesture (same as STATE_RECORDING)
 *   trainingPhase 2: SAMPLE_DONE — sample captured, preparing for next
 *
 * After 3 samples: compute average, save to SPIFFS, reload gestures.
 * Button B cancels training at any time.
 */
static void handleTrainingState() {
    // Button B: cancel training at any time
    if (M5.BtnB.wasClicked()) {
        cancelTrainingMode();
        return;
    }

    switch (trainingPhase) {

    // -- Phase 0: Waiting for user to do double-tap before gesture --
    case 0: {
        if (detectDoubleTap()) {
            // FIX INSTALL-16: Countdown de 3 segundos antes de gravar.
            // Separa o movimento do double-tap do gesto real.
            // Mostra 3... 2... 1... GO! na tela para o usuario se preparar.
            trainingCountdownStart = millis();
            trainingLastCountdownNum = 3;
            trainingPhase = 1;
            audioPlayer.beepConfirm();

            int sampleNum = gestureEngine.getTrainingSampleCount() + 1;
            Serial.printf("[MAIN] Training: countdown started for sample %d/3\n", sampleNum);

            // Mostra "3" na tela
            StickCP2.Display.fillScreen(TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(4);
            StickCP2.Display.drawString("3", 120, 67);
        }
        break;
    }

    // -- Phase 1: Countdown 3-2-1-GO antes de gravar --
    case 1: {
        unsigned long elapsed = millis() - trainingCountdownStart;
        int countdownNum = 3 - static_cast<int>(elapsed / 1000);

        if (countdownNum <= 0) {
            // Countdown terminou — iniciar gravacao do gesto real
            gestureEngine.startTrainingSample();  // Reset + clear trajectory
            trainingRecordStart = millis();
            trainingPhase = 2;

            // FIX INSTALL-16: Tela de gravação vermelha (treino)
            // Mostra "GRAVANDO" + "Faca o gesto agora!" em vermelho
            displayUI.showRecordingTraining();
            audioPlayer.beepConfirm();

            int sampleNum = gestureEngine.getTrainingSampleCount() + 1;
            Serial.printf("[MAIN] Training: recording sample %d/3 (after countdown)\n", sampleNum);
        } else if (countdownNum != trainingLastCountdownNum) {
            // Atualiza numero na tela
            trainingLastCountdownNum = countdownNum;
            StickCP2.Display.fillScreen(TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(4);
            char numStr[4];
            snprintf(numStr, sizeof(numStr), "%d", countdownNum);
            StickCP2.Display.drawString(numStr, 120, 67);
            audioPlayer.beepConfirm();
            Serial.printf("[MAIN] Training: countdown %d...\n", countdownNum);
        }
        break;
    }

    // -- Phase 2: Recording gesture sample --
    case 2: {
        unsigned long elapsed = millis() - trainingRecordStart;

        // FIX TRAIN-SENSOR: Log periodico mostra o sensor PRINCIPAL do gesto.
        // Contextos = Sensor A, Objetos = Sensor B.
        static unsigned long lastTrainLog = 0;
        if (millis() - lastTrainLog >= 500) {
            lastTrainLog = millis();
            bool isCtxLog = (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X');
            const auto& matrix = isCtxLog ? gestureEngine.getMatrixA() : gestureEngine.getMatrixB();
            Serial.printf("[REC] sensor=%s traj=%d mov=%.1f stable=%d elapsed=%dms\n",
                          isCtxLog ? "A(ctx)" : "B(obj)",
                          (int)matrix.getTrajectory().size(),
                          matrix.getTotalMovement(),
                          matrix.isStable(gestureEngine.getLevelConfig().stableDurationMs),
                          (int)elapsed);
        }

        // Check timeout
        if (elapsed > voiceManager.getLevelConfig().gestureTimeoutMs) {
            gestureEngine.stopTrainingSample();
            trainingPhase = 0;  // Back to waiting

            int sampleNum = gestureEngine.getTrainingSampleCount() + 1;
            displayUI.showTrainingWait(trainingGestureName, sampleNum, 3);
            displayUI.showToast("Tempo esgotado, tente novamente", 2000);
            audioPlayer.beepError();

            // FIX TRAIN-SENSOR: Diagnostico de timeout mostra sensor principal
            {
                bool isCtxTimeout = (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X');
                const auto& mTimeout = isCtxTimeout ? gestureEngine.getMatrixA() : gestureEngine.getMatrixB();
                Serial.printf("[MAIN] Training: timeout — sensor=%s traj=%d, movement=%.1f/%.1f, stable=%d, elapsed=%dms\n",
                              isCtxTimeout ? "A(ctx)" : "B(obj)",
                              (int)mTimeout.getTrajectory().size(),
                              mTimeout.getTotalMovement(),
                              isCtxTimeout ? MIN_GESTURE_MOVEMENT : 2.0f,
                              mTimeout.isStable(gestureEngine.getLevelConfig().stableDurationMs),
                              (int)elapsed);
            }
            break;
        }

        // Check if gesture recording is naturally complete
        if (gestureEngine.isGestureComplete()) {
            gestureEngine.stopTrainingSample();

            // Try to add this sample
            if (gestureEngine.addTrainingSample()) {
                int sampleCount = gestureEngine.getTrainingSampleCount();

                // Notify config handler (sends event to app)
                configHandler.notifyTrainingSampleCaptured();

                if (gestureEngine.hasEnoughSamples()) {
                    // All 3 samples collected — compute and save
                    trainingPhase = 3;
                    displayUI.showProcessing();
                    Serial.println("[MAIN] Training: all 3 samples collected, computing...");
                } else {
                    // Show success briefly, then display prompt for next sample
                    displayUI.showTrainingSampleOk(sampleCount, 3);
                    audioPlayer.beepConfirm();

                    // Go back to waiting for tap — show next sample prompt
                    trainingPhase = 0;
                    int nextSample = sampleCount + 1;
                    displayUI.showTrainingWait(trainingGestureName, nextSample, 3);

                    Serial.printf("[MAIN] Training: sample %d/3 OK\n", sampleCount);
                }
            } else {
                // Sample rejected (too short, etc.) — retry
                trainingPhase = 0;
                int sampleNum = gestureEngine.getTrainingSampleCount() + 1;
                displayUI.showTrainingWait(trainingGestureName, sampleNum, 3);
                displayUI.showToast("Movimento insuficiente! Faca gesto maior", 2000);
                audioPlayer.beepError();

                // FIX TRAIN-SENSOR: Diagnostico de rejeicao mostra sensor principal
                {
                    bool isCtxRej = (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X');
                    const auto& mRej = isCtxRej ? gestureEngine.getMatrixA() : gestureEngine.getMatrixB();
                    Serial.printf("[MAIN] Training: sample rejected — sensor=%s traj=%d, movement=%.1f (min %.1f)\n",
                                  isCtxRej ? "A(ctx)" : "B(obj)",
                                  (int)mRej.getTrajectory().size(),
                                  mRej.getTotalMovement(),
                                  isCtxRej ? MIN_GESTURE_MOVEMENT : 2.0f);
                }
            }
        }
        break;
    }

    // -- Phase 3: Computing average and saving --
    case 3: {
        // Compute the average gesture from the 3 samples
        GestureDefinition trained = gestureEngine.computeAverageGesture();

        // Salva trajetorias treinadas no SPIFFS
        // FIX M10: Passa threshold calculado para ser salvo no JSON.
        // FIX INSTALL-20: Salvar trajetorias de accel + gyro
        // Modelo Orbital: passa assinaturas orbitais para persistencia no SPIFFS
        bool saved = false;

        // FIX AUDIO-05: Contextos (CXxx) usam saveContextTrajectory porque
        // o formato do contexts.json e diferente (trajectory, nao trajectory_a/b).
        if (trainingGestureId[0] == 'C' && trainingGestureId[1] == 'X') {
            // Extrair ID numerico do contexto (CX01 -> "1")
            char ctxIdStr[8];
            snprintf(ctxIdStr, sizeof(ctxIdStr), "%d", atoi(trainingGestureId + 2));
            // FIX FLOW-04: Contextos sao do Sensor A (HAT, mao esquerda).
            // FIX FLOW-07: Salvar assinatura orbital junto com trajetoria.
            // Sem assinatura, o matching usa fallback que aceita qualquer gesto.
            saved = gestureEngine.getLoader().saveContextTrajectory(
                ctxIdStr,
                trained.trajectoryA,
                trained.durationMs,
                trained.signatureA.valid ? &trained.signatureA : nullptr);
        } else {
            saved = gestureEngine.getLoader().saveGestureTrajectory(
                trainingGestureId,
                trained.trajectoryA,
                trained.trajectoryB,
                trained.durationMs,
                trained.threshold,
                trained.trajectoryAGyro,
                trained.trajectoryBGyro,
                trained.signatureA.valid ? &trained.signatureA : nullptr,
                trained.signatureB.valid ? &trained.signatureB : nullptr,
                trained.signatureAGyro.valid ? &trained.signatureAGyro : nullptr);
        }

        if (saved) {

            // Notify config handler (sends event to app)
            configHandler.notifyTrainingComplete();

            displayUI.showTrainingDone(trainingGestureName, trained.threshold);
            audioPlayer.beepConfirm();

            Serial.printf("[MAIN] Training complete: %s saved (threshold=%.2f)\n",
                          trainingGestureName, trained.threshold);

            // Reload gestures to pick up the new trajectories
            gestureEngine.loadAllCategories();
            loadActiveProfileGestures();
        } else {
            displayUI.showTrainingFail("Erro ao salvar");
            audioPlayer.beepError();

            Serial.printf("[MAIN] Training FAILED: could not save gesture %s\n",
                          trainingGestureId);
        }

        // Limpa estado de treino
        gestureEngine.clearTrainingSamples();
        gestureEngine.setTrainingIsContext(false);  // FIX TRAIN-SENSOR: reset flag
        trainingActive = false;
        trainingGestureId[0] = '\0';
        trainingGestureName[0] = '\0';

        configHandler.resetTrainingState();

        // FIX M13: Mostra resultado por 2 segundos antes de voltar ao IDLE.
        // Antes: currentState = STATE_IDLE imediatamente, e na proxima
        // iteracao do loop() o IDLE redraw apagava a tela de resultado.
        // O usuario via o resultado por ~20ms (1 frame).
        // Agora: fase 3 = exibicao do resultado, espera 2s.
        trainingPhase = 4;
        _trainingResultTime = millis();

        break;
    }

    // Phase 4: Exibir resultado do treino por 2 segundos
    case 4: {
        if (millis() - _trainingResultTime >= 2000) {
            trainingPhase = 0;
            // Sprint C2: voltar pro menu se origem foi o menu local
            if (trainingReturnToMenu) {
                trainingReturnToMenu = false;
                menuUI.reopenAtTrainGesture();
                currentState = STATE_MENU;
                Serial.println("[MAIN] Training done — voltando pro SUB_TRAIN_LIST");
            } else {
                currentState = STATE_IDLE;
            }
        }
        break;
    }

    default:
        // Invalid phase — reset to safety
        cancelTrainingMode();
        break;
    }
}
