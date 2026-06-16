/**
 * GESTUUM — Gesture Engine Header
 * Bloco: 3.2 — Sensor A - Gesture Engine
 * Responsibility: Declare GestureEngine class that integrates Matrix3D + DTW
 *                 for gesture recognition against a loaded gesture database.
 */

#ifndef GESTUUM_GESTURE_ENGINE_H
#define GESTUUM_GESTURE_ENGINE_H

#include "../../shared/src/constants.h"
#include "../../shared/src/protocol.h"
#include "../../shared/src/matrix3d.h"
#include "../../shared/src/dtw.h"
#include "../../shared/src/gesture_data.h"
#include "../../shared/src/orbital_extractor.h"
#include "gesture_loader.h"
#include <vector>

/**
 * Hand dominance classification — which hand performed the gesture.
 * Determined by comparing total movement of matrixA vs matrixB.
 */
enum HandDominance : uint8_t {
    HAND_UNKNOWN = 0,   // Both hands near zero movement
    HAND_RIGHT   = 1,   // Sensor A — object gestures
    HAND_LEFT    = 2,   // Sensor B — context gestures
    HAND_BOTH    = 3    // Both hands — combined gestures
};

/**
 * Result of a gesture recognition attempt.
 * All fields are zeroed by processGesture() before population.
 */
struct GestureResult {
    bool matched;                // True if a gesture was recognized
    int gestureIndex;            // Index into the current gesture database
    float confidence;            // Confidence score (0.0 - 1.0)
    bool isSolo;                 // True if single-hand gesture
    bool isContext;              // True if matched gesture is a context prefix
    bool hasAutomation;          // True if gesture triggers automation
    AutomationCmd automationCmd; // Automation command (CMD_NONE if none)
    char gestureName[32];        // Human-readable gesture name
    char audioFile[32];          // Audio file to play on SPIFFS
    bool ambiguous;              // True if 2+ gestures scored similarly
    int contextIndex;            // Index into contexts vector (-1 if not a context)
    HandDominance handDominance; // Which hand was dominant during the gesture
};

/**
 * Result of an automation-only gesture check.
 * Returned by checkAutomationGestures() which runs every loop iteration.
 */
struct AutomationResult {
    bool triggered;              // True if an automation gesture was matched
    AutomationCmd command;       // The automation command to execute
};

/**
 * Core gesture recognition engine for the GESTUUM system.
 *
 * Integrates two Matrix3D instances (sensor A local, sensor B remote),
 * a DTW matcher, and a gesture database loaded per category.
 *
 * Usage:
 *   GestureEngine engine;
 *   engine.begin();
 *   engine.loadGesturesForCategory(CAT_GERAL);
 *   engine.loadGesturesForProfile(PROFILE_HOSPITAL);
 *   // Feed IMU data via updateSensorA/B during recording
 *   engine.startRecording();
 *   // ... collect samples ...
 *   // Check isGestureComplete() each loop iteration for early exit
 *   engine.stopRecording();
 *   GestureResult result = engine.processGesture();
 */
class GestureEngine {
public:
    GestureEngine();

    /**
     * Initialize engine: mount SPIFFS via GestureLoader, reset matrices,
     * set category to CAT_GERAL, and load gestures from JSON.
     */
    void begin();

    /**
     * Load gesture definitions for the given category.
     * Clears existing gestures and replaces with the new set.
     * Also resets baseGestureCount (profile gestures are cleared).
     */
    void loadGesturesForCategory(Category cat);

    /** Load ALL categories at once (flat list, no category navigation). */
    void loadAllCategories();

    /**
     * Load gesture definitions for the given profile and append them
     * to the current gesture list (after base category gestures).
     * @param profile Profile to load (PROFILE_HOSPITAL, etc.)
     */
    void loadGesturesForProfile(Profile profile);

    /**
     * Remove all profile gestures, keeping only the base category gestures.
     */
    void clearProfileGestures();

    /**
     * Load custom gesture definitions from /data/gestures/custom.json
     * and append them to the current gesture list.
     * Custom gestures are treated like profile gestures (appended after base).
     */
    void loadCustomGestures();

    /**
     * Load context definitions from /data/contexts.json.
     * Contexts are sentence prefixes (e.g., "Preciso de", "Estou com")
     * used in phrase building: context (left hand) + object (right hand).
     */
    void loadContextGestures();

    /**
     * Match sensor B trajectory against loaded context definitions.
     * @return Index into contexts vector if matched, -1 otherwise.
     */
    int matchContext();

    /**
     * Get context definition by index.
     * @param index Index into contexts vector.
     * @return Reference to the ContextDefinition.
     */
    const ContextDefinition& getContext(int index) const;

    /**
     * Get the number of loaded context definitions.
     */
    int getContextCount() const;

    /**
     * Feed local IMU acceleration data from sensor A.
     * Only records if currently in recording state.
     */
    void updateSensorA(float ax, float ay, float az);

    /**
     * FIX INSTALL-20: Feed gyroscope data from sensor A.
     * Updates the gyro grid (rotation trajectory) in parallel with accel grid.
     */
    void updateSensorAGyro(float gx, float gy, float gz);

    /**
     * Feed remote IMU acceleration data from sensor B.
     * Only records if currently in recording state.
     */
    void updateSensorB(float ax, float ay, float az);

    /**
     * FIX INSTALL-20: Feed gyroscope data from sensor B.
     */
    void updateSensorBGyro(float gx, float gy, float gz);

    /**
     * Start recording gesture data.
     * Resets and clears both matrix trajectories.
     * Records the start timestamp for duration checks.
     */
    void startRecording();

    /**
     * Stop recording gesture data.
     */
    void stopRecording();

    /**
     * Run DTW matching against the loaded gesture database.
     * Uses hand dominance detection to filter and weight matching.
     * Returns a GestureResult with match info.
     */
    GestureResult processGesture();

    /**
     * Detect which hand was dominant during the last gesture.
     * Compares total movement of matrixA (right) vs matrixB (left).
     */
    HandDominance detectDominantHand() const;

    /**
     * Check automation-only gestures against current sensor data.
     * This runs independently of the recording state, every loop iteration.
     * Uses the main matrixA/matrixB trajectories directly.
     * Only checks gestures that have automation_cmd != CMD_NONE and
     * empty audioFile (pure automation gestures).
     */
    AutomationResult checkAutomationGestures();

    /**
     * Check if the engine is currently recording.
     */
    bool isRecording() const;

    /**
     * Check if a gesture recording is naturally complete.
     * Returns true when ALL of:
     *   1. Recording duration >= MIN_RECORDING_DURATION_MS (800ms)
     *   2. matrixA has sufficient trajectory (>= DTW_MIN_TRAJECTORY_LEN points)
     *   3. matrixA.isStable(300) — user stopped moving for 300ms
     *
     * This allows main.cpp to exit RECORDING state early without
     * waiting for the full GESTURE_TIMEOUT_MS (3s) timeout.
     * The timeout remains as a fallback safety net.
     */
    bool isGestureComplete() const;

    /**
     * FIX FLOW-04: Informa ao engine que estamos esperando gesto do Sensor B
     * (segundo gesto de frase composta). isGestureComplete() vai esperar
     * especificamente pelo Sensor B em vez de aceitar Sensor A (double-tap).
     */
    void setWaitingForSensorB(bool waiting);
    bool _waitingForSensorB = false;

    /**
     * FIX TRAIN-SENSOR: Informa ao engine se o treino atual e de CONTEXTO ou OBJETO.
     * Contexto (CXxx) = Sensor A principal, Sensor B ignorado.
     * Objeto (Gxx)    = Sensor B principal, Sensor A ignorado.
     * Usado por addTrainingSample() e computeAverageGesture() para filtrar
     * dados do sensor correto e ignorar residuais do outro.
     */
    void setTrainingIsContext(bool isContext);
    bool _trainingIsContext = false;

    /**
     * Get the number of gestures loaded (base + profiles).
     */
    int getGestureCount() const;

    /**
     * Get a gesture definition by index.
     * Index must be in range [0, getGestureCount()).
     */
    const GestureDefinition& getGesture(int index) const;

    /**
     * Get the currently active category.
     */
    Category getCurrentCategory() const;

    /**
     * Check if gesture database loaded successfully and engine is ready.
     */
    bool isReady() const;

    /**
     * Get a reference to the gesture loader (for training save/query operations).
     */
    GestureLoader& getLoader();

    /**
     * Set the active level configuration for gesture capture sensitivity.
     * Adjusts min trajectory length, stable duration, min recording duration,
     * and DTW threshold multiplier.
     */
    void setLevelConfig(const LevelConfig& config);

    // === Training Mode ===

    /**
     * Begin recording one training sample.
     * Resets matrices and starts recording (same as startRecording).
     */
    void startTrainingSample();

    /**
     * Stop recording the current training sample.
     */
    void stopTrainingSample();

    /**
     * Store current trajectory as a training sample.
     * Copies matrixA and matrixB trajectories into the training buffer.
     * @return true if sample was added, false if buffer is full.
     */
    bool addTrainingSample();

    /**
     * Check if enough training samples have been collected.
     * @return true when TRAINING_SAMPLES_NEEDED (3) samples are stored.
     */
    bool hasEnoughSamples() const;

    /**
     * Compute a reference gesture from the collected training samples.
     * Uses the median-length sample as the reference trajectory.
     * Computes threshold from average DTW distance between samples x 1.5.
     * @return GestureDefinition with averaged trajectories and computed threshold.
     */
    GestureDefinition computeAverageGesture() const;

    /**
     * Reset all training state (clear sample buffers).
     */
    void clearTrainingSamples();

    /**
     * Get the number of training samples recorded so far.
     */
    int getTrainingSampleCount() const;

    // === Jaccard Similarity Matching ===

    /**
     * FIX INSTALL-18: Jaccard Similarity (mantido como fallback).
     */
    static float jaccardSimilarity(const std::deque<Point3D>& captured,
                                   const std::vector<Point3D>& reference);

    /**
     * FIX INSTALL-19: Comparacao por sequencia — reconhecimento deterministico.
     * Compara a trajetoria capturada com a de referencia PASSO A PASSO,
     * respeitando a ORDEM das celulas visitadas.
     *
     * Quando as trajetorias tem tamanhos diferentes, redimensiona por
     * indice proporcional (nearest neighbor interpolation).
     *
     * Cada passo e considerado "correto" se a celula capturada esta
     * dentro de ±1 celula da referencia em cada eixo (vizinhanca 3x3x3).
     *
     * @param captured  Trajetoria capturada (deque de pontos)
     * @param reference Trajetoria de referencia treinada (vector de pontos)
     * @return Score [0.0, 1.0] = proporcao de passos corretos na sequencia
     */
    static float sequenceSimilarity(const std::deque<Point3D>& captured,
                                    const std::vector<Point3D>& reference);

    // === Diagnostic Accessors ===

    /**
     * Get read-only reference to the internal Matrix3D for Sensor A.
     * Used for diagnostics: trajectory size, stability status, etc.
     */
    const Matrix3D& getMatrixA() const;

    /**
     * Get read-only reference to the gyro Matrix3D for Sensor A.
     */
    const Matrix3D& getMatrixAGyro() const;

    /**
     * Get read-only reference to the internal Matrix3D for Sensor B.
     */
    const Matrix3D& getMatrixB() const;

    /**
     * Get the active level configuration.
     */
    const LevelConfig& getLevelConfig() const;

    /**
     * Get the hand dominance detected during training samples.
     * Returns the dominance of the last recorded sample.
     */
    HandDominance getTrainingHandDominance() const;

private:
    Matrix3D matrixA;       // Sensor A aceleracao (local / dominant hand)
    Matrix3D matrixB;       // Sensor B aceleracao (remote / secondary hand)
    Matrix3D matrixAGyro;   // FIX INSTALL-20: Sensor A giroscopio (rotacao)
    Matrix3D matrixBGyro;   // FIX INSTALL-20: Sensor B giroscopio (rotacao)
    DTW dtw;
    GestureLoader loader;  // JSON-based gesture loader from SPIFFS

    std::vector<GestureDefinition> currentGestures;  // Gestures for active category + profiles
    std::vector<ContextDefinition> contexts;         // Context prefix definitions for phrase building
    Category currentCategory;
    bool recording;
    bool _ready;  // True if loader initialized successfully

    LevelConfig _levelConfig;  // Active gesture capture level configuration

    size_t baseGestureCount;  // Number of gestures from base category (before profile gestures)

    unsigned long recordingStartMs;  // Timestamp when recording started (for isGestureComplete)

    // Automation gesture tracking
    unsigned long lastAutomationCheckMs;  // Timestamp of last automation check
    unsigned long automationCooldownMs;   // Cooldown to prevent repeated triggers

    // Training mode state
    std::vector<std::vector<Point3D>> _trainingSamplesA;      // Accel sensor A
    std::vector<std::vector<Point3D>> _trainingSamplesB;      // Accel sensor B
    std::vector<std::vector<Point3D>> _trainingSamplesAGyro;  // FIX INSTALL-20: Gyro sensor A
    std::vector<std::vector<Point3D>> _trainingSamplesBGyro;  // FIX INSTALL-20: Gyro sensor B
    static const int TRAINING_SAMPLES_NEEDED = 3;

    // === Modelo Orbital — assinaturas capturadas e de treino ===
    OrbitalSignature _capturedSigA;                           // Ultima captura sensor A (accel)
    OrbitalSignature _capturedSigB;                           // Ultima captura sensor B (accel)
    OrbitalSignature _capturedSigAGyro;                       // Ultima captura sensor A (gyro)
    OrbitalSignature _capturedSigBGyro;                       // FIX BUG-02: Captura sensor B (gyro)
    OrbitalSignature _trainingSignaturesA[TRAINING_SAMPLES_NEEDED];    // Treino accel A
    OrbitalSignature _trainingSignaturesB[TRAINING_SAMPLES_NEEDED];    // Treino accel B
    OrbitalSignature _trainingSignaturesAGyro[TRAINING_SAMPLES_NEEDED]; // Treino gyro A
};

#endif // GESTUUM_GESTURE_ENGINE_H
