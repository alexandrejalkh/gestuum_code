/**
 * GESTUUM — Display UI Implementation for Sensor A (M5StickC Plus2)
 * Bloco: 3.4 — Display UI (visual interface on TFT screen)
 * Responsibility: Full implementation of all screen states, transitions,
 *                 and animations on the M5StickC Plus2 TFT display.
 *
 * Display API: StickCP2.Display (M5GFX-based)
 * Screen: 135x240 physical, 240x135 after setRotation(1)
 *
 * Fix B9: forceConnectionUpdate() allows header redraw on any screen
 *         (except SCREEN_EMERGENCY) when connection state changes.
 */

#include "display_ui.h"
#include <cstring>

// === Category name lookup table ===
static const char* const CATEGORY_NAMES[CAT_COUNT] = {
    "GERAL",
    "EMERGENCIA",
    "CASA",
    "TRABALHO",
    "SOCIAL"
};

// === Category color lookup table (RGB565) ===
static const uint16_t CATEGORY_COLORS_565[CAT_COUNT] = {
    TFT_CAT_GERAL,       // Blue
    TFT_CAT_EMERGENCIA,  // Red
    TFT_CAT_CASA,        // Green
    TFT_CAT_TRABALHO,    // Yellow
    TFT_CAT_SOCIAL       // Purple
};

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------
DisplayUI::DisplayUI()
    : _currentScreen(SCREEN_PAIRING)
    , _currentCategory(CAT_GERAL)
    , _batteryLevel(0)
    , _isConnected(false)
    , _matchedConfidence(0.0f)
    , _forceHeaderRedraw(false)
    , _currentVoice(VOICE_HOMEM)
    , _toastActive(false)
    , _toastStartMs(0)
    , _toastDurationMs(0)
    , _screenEnteredAt(0)
    , _lastToggle(0)
    , _toggleState(false)
    , _dotCount(0)
    , _lastDotUpdate(0)
    , _trainingSampleNum(0)
    , _trainingTotalSamples(0)
    , _trainingConfidence(0.0f)
{
    _lastGestureName[0] = '\0';
    _matchedGestureName[0] = '\0';
    _errorMsg[0] = '\0';
    _toastMsg[0] = '\0';
    _trainingGestureName[0] = '\0';
    _trainingFailReason[0] = '\0';
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void DisplayUI::begin() {
    StickCP2.Display.setRotation(1);
    StickCP2.Display.setTextWrap(false);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.fillScreen(TFT_BLACK);
}

void DisplayUI::update() {
    unsigned long now = millis();

    // Fix B9: If a forced header redraw is pending, redraw the top 20px header
    // on any screen except SCREEN_EMERGENCY (which uses its own full-screen draw).
    if (_forceHeaderRedraw) {
        _forceHeaderRedraw = false;
        if (_currentScreen != SCREEN_EMERGENCY) {
            drawHeader(_currentCategory, _isConnected, _batteryLevel);
        }
    }

    switch (_currentScreen) {

    case SCREEN_EMERGENCY: {
        // Toggle background red/black at EMERGENCY_TOGGLE_MS interval
        if (now - _lastToggle >= EMERGENCY_TOGGLE_MS) {
            _lastToggle = now;
            _toggleState = !_toggleState;

            uint16_t bgColor = _toggleState ? TFT_RED : TFT_BLACK;
            StickCP2.Display.fillScreen(bgColor);

            // Redraw centered text every toggle
            StickCP2.Display.setTextColor(TFT_WHITE, bgColor);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(3);
            StickCP2.Display.drawString("SOCORRO!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

            StickCP2.Display.setTextSize(1);
            StickCP2.Display.drawString("[B 2s: sair]", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15);
        }
        break;
    }

    case SCREEN_MATCHED: {
        // Return to IDLE after MATCHED_DISPLAY_MS
        if (now - _screenEnteredAt >= MATCHED_DISPLAY_MS) {
            showIdle(_currentCategory, _isConnected, _batteryLevel);
        }
        break;
    }

    case SCREEN_NO_MATCH: {
        // Return to IDLE after NO_MATCH_DISPLAY_MS
        if (now - _screenEnteredAt >= NO_MATCH_DISPLAY_MS) {
            showIdle(_currentCategory, _isConnected, _batteryLevel);
        }
        break;
    }

    case SCREEN_PAIRING: {
        // Animate dots: . .. ...
        if (now - _lastDotUpdate >= DOTS_ANIM_MS) {
            _lastDotUpdate = now;
            _dotCount = (_dotCount % 3) + 1;

            // Clamp _dotCount to max 3 before using as array index (fix H2)
            if (_dotCount > 3) {
                _dotCount = 3;
            }

            // Build dots string
            char dots[4];
            for (uint8_t i = 0; i < _dotCount; i++) {
                dots[i] = '.';
            }
            dots[_dotCount] = '\0';

            // Clear only the dots area (bottom portion)
            StickCP2.Display.fillRect(0, SCREEN_HEIGHT / 2 + 15, SCREEN_WIDTH, 30, TFT_BLACK);

            StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.drawString(dots, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 25);
        }
        break;
    }

    case SCREEN_RECORDING: {
        // FIX INSTALL-17: Pulse "LENDO..." em ciano (leitura/reconhecimento)
        if (now - _lastToggle >= RECORDING_PULSE_MS) {
            _lastToggle = now;
            _toggleState = !_toggleState;

            uint16_t textColor = _toggleState ? TFT_CYAN : 0x0410; // Bright cyan / dark cyan
            StickCP2.Display.setTextColor(textColor, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(3);
            StickCP2.Display.drawString("LENDO...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 5);
        }

        // Animate a simple progress bar at the bottom
        {
            unsigned long elapsed = now - _screenEnteredAt;
            uint16_t barWidth = (uint16_t)((elapsed % 3000UL) * (SCREEN_WIDTH - 40) / 3000UL);
            StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 20, SCREEN_WIDTH - 40, 8, TFT_DARKGREY);
            StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 20, barWidth, 8, TFT_CYAN);
        }
        break;
    }

    // FIX INSTALL-17: Animacao do TRAINING_RECORDING (vermelho)
    case SCREEN_TRAINING_RECORDING: {
        if (now - _lastToggle >= RECORDING_PULSE_MS) {
            _lastToggle = now;
            _toggleState = !_toggleState;

            uint16_t textColor = _toggleState ? TFT_RED : 0x7800;
            StickCP2.Display.setTextColor(textColor, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(3);
            StickCP2.Display.drawString("GRAVANDO", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 5);
        }

        // Progress bar vermelho
        {
            unsigned long elapsed = now - _screenEnteredAt;
            uint16_t barWidth = (uint16_t)((elapsed % 5000UL) * (SCREEN_WIDTH - 40) / 5000UL);
            StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 25, SCREEN_WIDTH - 40, 8, TFT_DARKGREY);
            StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 25, barWidth, 8, TFT_RED);
        }
        break;
    }

    case SCREEN_PROCESSING: {
        // Animate a simple spinning indicator: - \ | /
        if (now - _lastDotUpdate >= 150) {
            _lastDotUpdate = now;
            _dotCount = (_dotCount + 1) % 4;

            static const char spinChars[] = "-\\|/";
            char spinStr[2] = { spinChars[_dotCount], '\0' };

            // Clear spinner area and redraw
            StickCP2.Display.fillRect(SCREEN_WIDTH / 2 - 10, SCREEN_HEIGHT / 2 + 10, 20, 20, TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_CYAN, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(2);
            StickCP2.Display.drawString(spinStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
        }
        break;
    }

    case SCREEN_CONTEXT_WAIT: {
        // Animate the "___" placeholder with pulsing dots
        if (now - _lastDotUpdate >= DOTS_ANIM_MS) {
            _lastDotUpdate = now;
            _dotCount = (_dotCount % 3) + 1;

            // Build animated blank: "_" -> "__" -> "___"
            char blank[4];
            for (uint8_t i = 0; i < _dotCount; i++) {
                blank[i] = '_';
            }
            blank[_dotCount] = '\0';

            // Clear object area and redraw
            StickCP2.Display.fillRect(20, SCREEN_HEIGHT / 2 + 2, SCREEN_WIDTH - 40, 28, TFT_BLACK);
            StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(3);
            StickCP2.Display.drawString(blank, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15);
        }
        break;
    }

    case SCREEN_TRAINING_WAIT: {
        // Pulse instruction text visibility
        if (now - _lastToggle >= TRAINING_PULSE_MS) {
            _lastToggle = now;
            _toggleState = !_toggleState;

            uint16_t textColor = _toggleState ? TFT_WHITE : 0x4A49; // White / dim grey
            // Clear instruction area
            StickCP2.Display.fillRect(0, SCREEN_HEIGHT - 25, SCREEN_WIDTH, 20, 0x0A29); // dark blue bg
            StickCP2.Display.setTextColor(textColor, 0x0A29);
            StickCP2.Display.setTextDatum(MC_DATUM);
            StickCP2.Display.setTextSize(1);
            StickCP2.Display.drawString("Faca double-tap e o gesto", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15);
        }
        break;
    }

    // SCREEN_TRAINING_RECORDING handled above (after SCREEN_RECORDING)

    case SCREEN_TRAINING_SAMPLE_OK: {
        // Auto-transition after TRAINING_SAMPLE_OK_MS (1.5s)
        if (now - _screenEnteredAt >= TRAINING_SAMPLE_OK_MS) {
            // Return to training wait for next sample, or stay if caller handles it
            // Signal via setting screen to IDLE so caller can detect and advance
            showIdle(_currentCategory, _isConnected, _batteryLevel);
        }
        break;
    }

    default:
        // SCREEN_IDLE, SCREEN_ERROR, SCREEN_TRAINING_DONE, SCREEN_TRAINING_FAIL:
        // no periodic animation needed
        break;
    }

    // Fix UX8: Auto-clear toast overlay after its duration expires
    if (_toastActive) {
        if (now - _toastStartMs >= _toastDurationMs) {
            _toastActive = false;
            // Clear toast bar area at bottom of screen
            StickCP2.Display.fillRect(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, 22, TFT_BLACK);
        }
    }
}

// ---------------------------------------------------------------------------
// Screen transitions
// ---------------------------------------------------------------------------

void DisplayUI::showPairing() {
    _currentScreen = SCREEN_PAIRING;
    _screenEnteredAt = millis();
    _lastDotUpdate = millis();
    _dotCount = 0;

    clearScreen(TFT_BLACK);
    // UX: Barra amarela = pareando (buscando sensor B)
    drawStatusBar(TFT_YELLOW);

    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);

    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("Buscando", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 15);
    StickCP2.Display.drawString("Sensor B...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 10);
}

void DisplayUI::showIdle(Category cat, bool connected, int battery) {
    _currentScreen = SCREEN_IDLE;
    _currentCategory = cat;
    _isConnected = connected;
    _batteryLevel = battery;
    _screenEnteredAt = millis();

    clearScreen(TFT_BLACK);

    // UX: Barra verde = pronto para receber gesto
    drawStatusBar(TFT_GREEN);

    // Draw header bar with category color, connection, battery
    drawHeader(cat, connected, battery);

    // Center area: last recognized gesture or "Pronto"
    const char* centerText = (_lastGestureName[0] != '\0') ? _lastGestureName : "Pronto";
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString(centerText, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 10);
}

void DisplayUI::showRecording() {
    // FIX INSTALL-16: Ciano = leitura/reconhecimento de gesto
    _currentScreen = SCREEN_RECORDING;
    _screenEnteredAt = millis();
    _lastToggle = millis();
    _toggleState = true;

    clearScreen(TFT_BLACK);

    // Barra ciano = lendo gesto para reconhecimento
    drawStatusBar(TFT_CYAN);

    // Borda ciano
    StickCP2.Display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_CYAN);
    StickCP2.Display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, TFT_CYAN);

    // Texto "LENDO..."
    StickCP2.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.drawString("LENDO...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 5);

    // Barra de progresso
    StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 20, SCREEN_WIDTH - 40, 8, TFT_DARKGREY);
}

void DisplayUI::showRecordingTraining() {
    // FIX INSTALL-16: Vermelho = gravacao de treino (salvando gesto)
    _currentScreen = SCREEN_TRAINING_RECORDING;
    _screenEnteredAt = millis();
    _lastToggle = millis();
    _toggleState = true;

    clearScreen(TFT_BLACK);

    // Barra vermelha = gravando gesto para treino
    drawStatusBar(TFT_RED);

    // Borda vermelha
    StickCP2.Display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_RED);
    StickCP2.Display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, TFT_RED);

    // Texto "GRAVANDO"
    StickCP2.Display.setTextColor(TFT_RED, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.drawString("GRAVANDO", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 5);

    // Instrucao
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.drawString("Faca o gesto agora!", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15);

    // Barra de progresso
    StickCP2.Display.fillRect(20, SCREEN_HEIGHT - 25, SCREEN_WIDTH - 40, 8, TFT_DARKGREY);
}

void DisplayUI::showMatched(const char* gestureName, float confidence) {
    _currentScreen = SCREEN_MATCHED;
    _screenEnteredAt = millis();

    // Store matched gesture data
    strncpy(_matchedGestureName, gestureName, sizeof(_matchedGestureName) - 1);
    _matchedGestureName[sizeof(_matchedGestureName) - 1] = '\0';
    _matchedConfidence = confidence;

    // Also update last gesture for IDLE screen
    strncpy(_lastGestureName, gestureName, sizeof(_lastGestureName) - 1);
    _lastGestureName[sizeof(_lastGestureName) - 1] = '\0';

    // Background: tinted with category color
    clearScreen(TFT_BLACK);

    // UX: Barra azul = gesto reconhecido, tocando audio
    drawStatusBar(TFT_BLUE);

    // Gesture name (large) — Fix UX5: adaptive text size for long names
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    uint8_t nameSize = (strlen(gestureName) <= 10) ? 3 : 2;
    StickCP2.Display.setTextSize(nameSize);
    StickCP2.Display.drawString(gestureName, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

    // Confidence percentage
    char confStr[16];
    int confPercent = (int)(confidence * 100.0f);
    snprintf(confStr, sizeof(confStr), "%d%% certeza", confPercent);

    StickCP2.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(confStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
}

void DisplayUI::showNoMatch() {
    _currentScreen = SCREEN_NO_MATCH;
    _screenEnteredAt = millis();

    clearScreen(TFT_BLACK);
    // UX: Barra vermelha = nao reconheceu (volta pro verde em 1s)
    drawStatusBar(TFT_RED);

    // Large question mark
    StickCP2.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(4);
    StickCP2.Display.drawString("?", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 15);

    // Instruction
    StickCP2.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString("Tente novamente", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
}

void DisplayUI::showEmergency() {
    _currentScreen = SCREEN_EMERGENCY;
    _screenEnteredAt = millis();
    _lastToggle = millis();
    _toggleState = true;

    // Initial draw: red background
    StickCP2.Display.fillScreen(TFT_RED);
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_RED);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.drawString("SOCORRO!", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString("[B 2s: sair]", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15);
}

void DisplayUI::showError(const char* msg) {
    _currentScreen = SCREEN_ERROR;
    _screenEnteredAt = millis();

    strncpy(_errorMsg, msg, sizeof(_errorMsg) - 1);
    _errorMsg[sizeof(_errorMsg) - 1] = '\0';

    clearScreen(TFT_RED);

    StickCP2.Display.setTextColor(TFT_WHITE, TFT_RED);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("ERRO", SCREEN_WIDTH / 2, 20);

    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(msg, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 5);
}

void DisplayUI::showProcessing() {
    _currentScreen = SCREEN_PROCESSING;
    _screenEnteredAt = millis();
    _dotCount = 0;
    _lastDotUpdate = millis();

    clearScreen(TFT_BLACK);
    // UX: Barra azul = processando (DTW rodando)
    drawStatusBar(TFT_BLUE);

    // "Processando..." centered on screen
    StickCP2.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("Processando...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

    // Draw initial spinner frame (simple rotating dash)
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("-", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);
}

void DisplayUI::showContextWait(const char* contextName) {
    _currentScreen = SCREEN_CONTEXT_WAIT;
    _screenEnteredAt = millis();
    _lastDotUpdate = millis();
    _dotCount = 0;

    clearScreen(TFT_BLACK);
    // UX: Barra laranja = aguardando gesto do objeto (contexto detectado)
    drawStatusBar(TFT_ORANGE);

    // Draw a cyan border to indicate context wait mode
    StickCP2.Display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, TFT_CYAN);
    StickCP2.Display.drawRect(1, 1, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2, TFT_CYAN);

    // Context name at top center
    StickCP2.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    uint8_t nameSize = (strlen(contextName) <= 12) ? 2 : 1;
    StickCP2.Display.setTextSize(nameSize);
    StickCP2.Display.drawString(contextName, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 20);

    // Placeholder for object: "___"
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextSize(3);
    StickCP2.Display.drawString("___", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 15);

    // Instruction at bottom
    StickCP2.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString("Faca o gesto do objeto", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 12);
}

// ---------------------------------------------------------------------------
// Training mode screens
// ---------------------------------------------------------------------------

void DisplayUI::showTrainingWait(const char* gestureName, int sampleNum, int totalSamples) {
    _currentScreen = SCREEN_TRAINING_WAIT;
    _screenEnteredAt = millis();
    _lastToggle = millis();
    _toggleState = true;
    _trainingSampleNum = sampleNum;
    _trainingTotalSamples = totalSamples;

    strncpy(_trainingGestureName, gestureName, sizeof(_trainingGestureName) - 1);
    _trainingGestureName[sizeof(_trainingGestureName) - 1] = '\0';

    // Background: dark blue (#0D47A1 -> RGB565 approx 0x0A29)
    clearScreen(0x0A29);

    // Top: "TREINO" in white, bold (large size)
    StickCP2.Display.setTextColor(TFT_WHITE, 0x0A29);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("TREINO", SCREEN_WIDTH / 2, 18);

    // Middle: gesture name in large yellow text
    StickCP2.Display.setTextColor(TFT_YELLOW, 0x0A29);
    uint8_t nameSize = (strlen(gestureName) <= 10) ? 3 : 2;
    StickCP2.Display.setTextSize(nameSize);
    StickCP2.Display.drawString(gestureName, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 10);

    // Bottom: "Amostra X de Y" in white
    char sampleStr[32];
    snprintf(sampleStr, sizeof(sampleStr), "Amostra %d de %d", sampleNum, totalSamples);
    StickCP2.Display.setTextColor(TFT_WHITE, 0x0A29);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(sampleStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 20);

    // Instruction (animated via update())
    StickCP2.Display.drawString("Faca double-tap e o gesto", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 15);
}

void DisplayUI::showTrainingSampleOk(int sampleNum, int totalSamples) {
    _currentScreen = SCREEN_TRAINING_SAMPLE_OK;
    _screenEnteredAt = millis();
    _trainingSampleNum = sampleNum;
    _trainingTotalSamples = totalSamples;

    // Background: green (#2E7D32 -> RGB565 approx 0x2EC6)
    clearScreen(0x2EC6);

    // Large checkmark
    StickCP2.Display.setTextColor(TFT_WHITE, 0x2EC6);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(4);
    StickCP2.Display.drawString("OK", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 25);

    // "Amostra X capturada!"
    char capturedStr[32];
    snprintf(capturedStr, sizeof(capturedStr), "Amostra %d capturada!", sampleNum);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(capturedStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 10);

    // Conditional message
    if (sampleNum < totalSamples) {
        StickCP2.Display.setTextColor(TFT_WHITE, 0x2EC6);
        StickCP2.Display.drawString("Prepare-se para a proxima...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 28);
    } else {
        StickCP2.Display.setTextColor(TFT_WHITE, 0x2EC6);
        StickCP2.Display.drawString("Processando...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 28);
    }
}

void DisplayUI::showTrainingDone(const char* gestureName, float confidence) {
    _currentScreen = SCREEN_TRAINING_DONE;
    _screenEnteredAt = millis();
    _trainingConfidence = confidence;

    strncpy(_trainingGestureName, gestureName, sizeof(_trainingGestureName) - 1);
    _trainingGestureName[sizeof(_trainingGestureName) - 1] = '\0';

    // Background: green (#2E7D32 -> RGB565 approx 0x2EC6)
    clearScreen(0x2EC6);

    // Large checkmark
    StickCP2.Display.setTextColor(TFT_WHITE, 0x2EC6);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(4);
    StickCP2.Display.drawString("OK", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 30);

    // "[gestureName] treinado!" in white bold
    char doneStr[48];
    snprintf(doneStr, sizeof(doneStr), "%s treinado!", gestureName);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString(doneStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 5);

    // "Confianca: XX%" in light green
    char confStr[24];
    int confPercent = (int)(confidence * 100.0f);
    snprintf(confStr, sizeof(confStr), "Confianca: %d%%", confPercent);
    StickCP2.Display.setTextColor(0x87F0, 0x2EC6); // Light green on green bg
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(confStr, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 28);

    // "Pressione B para sair" at bottom
    StickCP2.Display.setTextColor(TFT_WHITE, 0x2EC6);
    StickCP2.Display.drawString("Pressione B para sair", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 12);
}

void DisplayUI::showTrainingFail(const char* reason) {
    _currentScreen = SCREEN_TRAINING_FAIL;
    _screenEnteredAt = millis();

    strncpy(_trainingFailReason, reason, sizeof(_trainingFailReason) - 1);
    _trainingFailReason[sizeof(_trainingFailReason) - 1] = '\0';

    // Background: red (#D32F2F -> RGB565 approx 0xD8E4)
    clearScreen(0xD8E4);

    // Large X mark
    StickCP2.Display.setTextColor(TFT_WHITE, 0xD8E4);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(4);
    StickCP2.Display.drawString("X", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 - 30);

    // "Falha no treino" in white bold
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("Falha no treino", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 5);

    // Reason text
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(reason, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2 + 28);

    // "Pressione B para tentar novamente"
    StickCP2.Display.setTextColor(TFT_WHITE, 0xD8E4);
    StickCP2.Display.drawString("Pressione B para tentar novamente", SCREEN_WIDTH / 2, SCREEN_HEIGHT - 12);
}

void DisplayUI::flashTapHint() {
    // Draw a small white circle at center of screen (6px radius)
    StickCP2.Display.fillCircle(SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2, 6, TFT_WHITE);
    // The circle will be cleared on next full screen redraw (showIdle, showRecording, etc.)
}

// ---------------------------------------------------------------------------
// Partial updates (only effective in SCREEN_IDLE)
// ---------------------------------------------------------------------------

void DisplayUI::updateCategory(Category cat) {
    _currentCategory = cat;
    if (_currentScreen == SCREEN_IDLE) {
        showIdle(cat, _isConnected, _batteryLevel);
    }
}

void DisplayUI::updateBattery(int level) {
    _batteryLevel = level;
    if (_currentScreen == SCREEN_IDLE) {
        // Redraw header only
        drawHeader(_currentCategory, _isConnected, level);
    }
}

void DisplayUI::updateConnection(bool connected) {
    _isConnected = connected;
    if (_currentScreen == SCREEN_IDLE) {
        drawHeader(_currentCategory, connected, _batteryLevel);
    }
}

void DisplayUI::setLastGesture(const char* name) {
    strncpy(_lastGestureName, name, sizeof(_lastGestureName) - 1);
    _lastGestureName[sizeof(_lastGestureName) - 1] = '\0';
}

// ---------------------------------------------------------------------------
// Fix B9: Force connection update on any screen except SCREEN_EMERGENCY
// ---------------------------------------------------------------------------

/**
 * Called from main.cpp when ESP-NOW connection state changes
 * (connected -> disconnected or vice versa).
 *
 * Stores the new connection state and sets a flag so that the next
 * update() call redraws just the header bar (top 20px) regardless
 * of which screen is currently active — except SCREEN_EMERGENCY,
 * which uses its own full-screen toggling draw and would be disrupted
 * by a partial header redraw.
 */
void DisplayUI::forceConnectionUpdate(bool connected) {
    _isConnected = connected;
    _forceHeaderRedraw = true;
}

// ---------------------------------------------------------------------------
// Fix UX2: Voice indicator setter
// ---------------------------------------------------------------------------

void DisplayUI::setVoiceIndicator(Voice voice) {
    _currentVoice = voice;
    if (_currentScreen == SCREEN_IDLE) {
        drawHeader(_currentCategory, _isConnected, _batteryLevel);
    }
}

// ---------------------------------------------------------------------------
// Fix UX8: Show a temporary toast message at the bottom of the screen
// ---------------------------------------------------------------------------

/**
 * Draw a dark semi-transparent bar at the bottom of the screen with white text.
 * The toast auto-clears after durationMs via the update() method.
 */
void DisplayUI::showToast(const char* msg, unsigned long durationMs) {
    _toastActive = true;
    _toastStartMs = millis();
    _toastDurationMs = durationMs;
    strncpy(_toastMsg, msg, sizeof(_toastMsg) - 1);
    _toastMsg[sizeof(_toastMsg) - 1] = '\0';

    // Draw dark bar at bottom of screen (simulate semi-transparent overlay)
    StickCP2.Display.fillRect(0, SCREEN_HEIGHT - 22, SCREEN_WIDTH, 22, 0x2104);  // Dark grey

    // Draw white text centered in the bar
    StickCP2.Display.setTextColor(TFT_WHITE, 0x2104);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(_toastMsg, SCREEN_WIDTH / 2, SCREEN_HEIGHT - 11);
}

// ---------------------------------------------------------------------------
// Private drawing helpers
// ---------------------------------------------------------------------------

uint16_t DisplayUI::getCategoryColor565(Category cat) const {
    if (cat < CAT_COUNT) {
        return CATEGORY_COLORS_565[cat];
    }
    return TFT_WHITE;
}

const char* DisplayUI::getCategoryName(Category cat) const {
    if (cat < CAT_COUNT) {
        return CATEGORY_NAMES[cat];
    }
    return "???";
}

void DisplayUI::drawHeader(Category cat, bool connected, int battery) {
    // Clamp battery value to 0-100 before any calculation (fix M10)
    if (battery < 0) battery = 0;
    if (battery > 100) battery = 100;

    // Header bar: 20px tall
    uint16_t catColor = getCategoryColor565(cat);

    // Header: GESTUUM com cor da logo + bateria + conexao
    (void)catColor;  // Categorias removidas do MVP
    uint16_t logoColor = 0x7E54;  // #7BC09A em RGB565
    StickCP2.Display.fillRect(0, 0, SCREEN_WIDTH, 28, TFT_BLACK);
    // Nome GESTUUM grande
    StickCP2.Display.setTextColor(logoColor, TFT_BLACK);
    StickCP2.Display.setTextDatum(ML_DATUM);
    StickCP2.Display.setTextSize(2);
    StickCP2.Display.drawString("GESTUUM", 4, 14);

    // Connection indicator (right side)
    uint16_t connColor = connected ? TFT_GREEN : TFT_RED;
    const char* connIcon = connected ? "ON" : "--";
    StickCP2.Display.setTextColor(connColor, TFT_BLACK);
    StickCP2.Display.setTextDatum(MR_DATUM);
    StickCP2.Display.drawString(connIcon, SCREEN_WIDTH - 60, 10);

    // Fix UX2: Voice indicator between connection and battery
    // H=Homem, M=Mulher, m=Menino, f=Menina
    const char* voiceIcon;
    switch (_currentVoice) {
        case VOICE_HOMEM:  voiceIcon = "H"; break;
        case VOICE_MULHER: voiceIcon = "M"; break;
        case VOICE_MENINO: voiceIcon = "m"; break;
        case VOICE_MENINA: voiceIcon = "f"; break;
        default:           voiceIcon = "H"; break;
    }
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(voiceIcon, SCREEN_WIDTH - 55, 10);

    // Battery percentage
    char battStr[8];
    snprintf(battStr, sizeof(battStr), "%d%%", battery);

    // Battery bar background
    int barX = SCREEN_WIDTH - 50;
    int barY = 5;
    int barW = 30;
    int barH = 10;
    StickCP2.Display.drawRect(barX, barY, barW, barH, TFT_WHITE);

    // Battery bar fill (color based on level)
    uint16_t battColor;
    if (battery > 50) {
        battColor = TFT_GREEN;
    } else if (battery > 20) {
        battColor = TFT_YELLOW;
    } else {
        battColor = TFT_RED;
    }

    int fillW = (int)((long)battery * (barW - 2) / 100);
    if (fillW < 0) fillW = 0;
    if (fillW > barW - 2) fillW = barW - 2;
    StickCP2.Display.fillRect(barX + 1, barY + 1, fillW, barH - 2, battColor);

    // Battery nub
    StickCP2.Display.fillRect(barX + barW, barY + 2, 2, barH - 4, TFT_WHITE);

    // Battery text below bar
    StickCP2.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    StickCP2.Display.setTextDatum(MR_DATUM);
    StickCP2.Display.setTextSize(1);
    StickCP2.Display.drawString(battStr, SCREEN_WIDTH - 4, 10);

    // Separator line
    StickCP2.Display.drawFastHLine(0, 20, SCREEN_WIDTH, TFT_DARKGREY);
}

void DisplayUI::drawCenterText(const char* text, uint16_t color, uint8_t textSize) {
    StickCP2.Display.setTextColor(color, TFT_BLACK);
    StickCP2.Display.setTextDatum(MC_DATUM);
    StickCP2.Display.setTextSize(textSize);
    StickCP2.Display.drawString(text, SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
}

void DisplayUI::clearScreen(uint16_t color) {
    StickCP2.Display.fillScreen(color);
}

/**
 * Barra de status colorida no topo da tela (6px de altura, largura total).
 *
 * UX: Indica o estado do dispositivo de forma visivel a distancia.
 * O cuidador, professor ou colega consegue ver de longe se o sensor
 * esta pronto (verde), gravando (vermelho) ou falando (azul).
 *
 * Cores:
 *   TFT_GREEN  = PRONTO (pode fazer gesto)
 *   TFT_RED    = GRAVANDO (fazendo gesto, continue o movimento)
 *   TFT_BLUE   = FALANDO / PROCESSANDO (espere terminar)
 *   TFT_ORANGE = AGUARDANDO (contexto detectado, faca o gesto do objeto)
 *   TFT_YELLOW = PAREANDO (buscando sensor B)
 */
void DisplayUI::drawStatusBar(uint16_t color) {
    StickCP2.Display.fillRect(0, 0, SCREEN_WIDTH, 6, color);
}
