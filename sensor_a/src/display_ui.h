/**
 * GESTUUM — Display UI for Sensor A (M5StickC Plus2)
 * Bloco: 3.4 — Display UI (visual interface on TFT screen)
 * Responsibility: Manage all screen states, drawing, and animations
 *                 on the 135x240 TFT display (horizontal orientation).
 *
 * Fix B9: Added forceConnectionUpdate() to redraw header on any screen
 *         (except SCREEN_EMERGENCY) when connection state changes.
 */

#ifndef GESTUUM_DISPLAY_UI_H
#define GESTUUM_DISPLAY_UI_H

#include <M5StickCPlus2.h>
#include <constants.h>

// === Screen dimensions after rotation(1): 240 wide x 135 tall ===
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 135

// === Category colors in RGB565 format (for TFT display) ===
#define TFT_CAT_GERAL      0x001F  // Blue
#define TFT_CAT_EMERGENCIA  0xF800  // Red
#define TFT_CAT_CASA        0x07E0  // Green
#define TFT_CAT_TRABALHO    0xFFE0  // Yellow
#define TFT_CAT_SOCIAL      0x780F  // Purple

// === UI timing constants ===
#define EMERGENCY_TOGGLE_MS   200
#define MATCHED_DISPLAY_MS   2000
#define NO_MATCH_DISPLAY_MS  1000
#define DOTS_ANIM_MS          500
#define RECORDING_PULSE_MS    400
#define TRAINING_PULSE_MS     600
#define TRAINING_SAMPLE_OK_MS 1500

// === Screen states ===
enum ScreenState : uint8_t {
    SCREEN_PAIRING,     // Waiting for pairing
    SCREEN_IDLE,        // Main screen
    SCREEN_RECORDING,   // Recording gesture
    SCREEN_MATCHED,     // Gesture recognized
    SCREEN_NO_MATCH,    // Gesture not recognized
    SCREEN_EMERGENCY,   // Emergency mode
    SCREEN_ERROR,        // Error message
    SCREEN_PROCESSING,   // Processing gesture (DTW)
    SCREEN_CONTEXT_WAIT,       // Waiting for object gesture after context detected
    SCREEN_TRAINING_WAIT,      // Waiting for gesture sample
    SCREEN_TRAINING_RECORDING, // Recording gesture sample (reuses RECORDING style)
    SCREEN_TRAINING_SAMPLE_OK, // Sample captured, next
    SCREEN_TRAINING_DONE,      // Training complete!
    SCREEN_TRAINING_FAIL       // Training failed, retry
};

class DisplayUI {
public:
    DisplayUI();

    // Lifecycle
    void begin();
    void update();

    // Screen transitions
    void showPairing();
    void showIdle(Category cat, bool connected, int battery);
    void showRecording();           // Leitura/reconhecimento — ciano
    void showRecordingTraining();   // Gravacao/treino — vermelho
    void showMatched(const char* gestureName, float confidence);
    void showNoMatch();
    void showEmergency();
    void showError(const char* msg);
    void showProcessing();
    void showContextWait(const char* contextName);
    void flashTapHint();

    // Training mode screens
    void showTrainingWait(const char* gestureName, int sampleNum, int totalSamples);
    void showTrainingSampleOk(int sampleNum, int totalSamples);
    void showTrainingDone(const char* gestureName, float confidence);
    void showTrainingFail(const char* reason);

    // Impedir update() de sobrescrever tela customizada (Gesto 01/02)
    void suppressUpdates() { _currentScreen = SCREEN_IDLE; }

    // Partial updates (only effective in SCREEN_IDLE)
    void updateCategory(Category cat);
    void updateBattery(int level);
    void updateConnection(bool connected);
    void setLastGesture(const char* name);

    // Fix B9: Force connection update on any screen except SCREEN_EMERGENCY.
    // Called from main.cpp when ESP-NOW connection state changes.
    // Stores the new state and sets a flag so the next update() redraws the header.
    void forceConnectionUpdate(bool connected);

    // Fix UX2: Set voice indicator shown in the header bar
    void setVoiceIndicator(Voice voice);

    // Fix UX8: Show a temporary toast message overlaid at the bottom of the screen.
    // Auto-clears after durationMs via update().
    void showToast(const char* msg, unsigned long durationMs);

private:
    ScreenState _currentScreen;
    Category _currentCategory;
    char _lastGestureName[32];
    char _matchedGestureName[32];
    float _matchedConfidence;
    char _errorMsg[64];
    char _trainingGestureName[32];
    char _trainingFailReason[48];
    int _trainingSampleNum;
    int _trainingTotalSamples;
    float _trainingConfidence;
    int _batteryLevel;
    bool _isConnected;

    // Fix B9: Flag to force header redraw on next update() call
    bool _forceHeaderRedraw;

    // Fix UX2: Current voice for header indicator
    Voice _currentVoice;

    // Fix UX8: Toast overlay state
    bool _toastActive;
    unsigned long _toastStartMs;
    unsigned long _toastDurationMs;
    char _toastMsg[40];

    // Animation state
    unsigned long _screenEnteredAt;
    unsigned long _lastToggle;
    bool _toggleState;
    uint8_t _dotCount;
    unsigned long _lastDotUpdate;

    // Drawing helpers
    uint16_t getCategoryColor565(Category cat) const;
    const char* getCategoryName(Category cat) const;
    void drawHeader(Category cat, bool connected, int battery);
    void drawCenterText(const char* text, uint16_t color, uint8_t textSize);
    void clearScreen(uint16_t color);

    // UX: Barra de status colorida no topo da tela.
    // Verde = pronto (pode fazer gesto), Vermelho = gravando, Azul = falando.
    // Visivel a distancia — cuidador/professor sabe o estado do dispositivo.
    void drawStatusBar(uint16_t color);
};

#endif // GESTUUM_DISPLAY_UI_H
