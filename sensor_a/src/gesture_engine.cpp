/**
 * GESTUUM — Gesture Engine Implementation
 * Bloco: 3.2 / 5.1 — Sensor A - Gesture Engine
 * Responsibility: Integrate Matrix3D + DTW for gesture recognition.
 *                 Loads gesture database from JSON files via GestureLoader.
 *
 * Automation gestures (automation_cmd != CMD_NONE with empty audioFile)
 * are checked independently every loop iteration via checkAutomationGestures(),
 * using the main matrixA/matrixB trajectories directly.
 */

#include <Arduino.h>
#include "gesture_engine.h"
#include "config.h"
#include <cstring>
#include <cmath>

// Automation check interval (ms) — how often to run DTW on automation gestures
static const unsigned long AUTOMATION_CHECK_INTERVAL_MS = 200;

// Cooldown after an automation gesture triggers (ms) — prevent rapid re-triggers
static const unsigned long AUTOMATION_COOLDOWN_MS = 800;

// Stability duration for gesture completion detection (ms)
static const unsigned long GESTURE_STABLE_DURATION_MS = 300;

// ============================================================================
// Constructor
// ============================================================================

GestureEngine::GestureEngine()
    : currentCategory(CAT_GERAL)
    , recording(false)
    , _ready(false)
    , _levelConfig(LEVEL_CONFIGS[LEVEL_STANDARD])
    , baseGestureCount(0)
    , recordingStartMs(0)
    , lastAutomationCheckMs(0)
    , automationCooldownMs(0) {
}

// ============================================================================
// begin()
// ============================================================================

void GestureEngine::begin() {
    matrixA.reset();
    matrixB.reset();
    currentCategory = CAT_GERAL;
    recording = false;
    _ready = false;
    baseGestureCount = 0;
    recordingStartMs = 0;
    lastAutomationCheckMs = 0;
    automationCooldownMs = 0;

    if (!loader.begin()) {
        Serial.println("[GestureEngine] GestureLoader init failed — no gestures available");
        return;
    }

    _ready = true;
    loadAllCategories();
}

// ============================================================================
// loadGesturesForCategory()
// ============================================================================

void GestureEngine::loadGesturesForCategory(Category cat) {
    currentGestures.clear();
    currentCategory = cat;
    baseGestureCount = 0;

    if (!loader.loadCategory(cat, currentGestures)) {
        Serial.printf("[GestureEngine] Failed to load gestures for category %d\n",
                      static_cast<int>(cat));
    }

    baseGestureCount = currentGestures.size();
}

// Carrega TODAS as categorias de uma vez (flat list, sem navegacao)
void GestureEngine::loadAllCategories() {
    currentGestures.clear();
    currentCategory = CAT_GERAL;
    baseGestureCount = 0;

    for (int i = 0; i < CAT_COUNT; i++) {
        Category cat = static_cast<Category>(i);
        std::vector<GestureDefinition> catGestures;
        if (loader.loadCategory(cat, catGestures)) {
            for (auto& g : catGestures) {
                if (currentGestures.size() < MAX_GESTURES_TOTAL) {
                    currentGestures.push_back(g);
                }
            }
            Serial.printf("[GestureEngine] Loaded %d gestures from category %d (total: %d)\n",
                          (int)catGestures.size(), i, (int)currentGestures.size());
        }
    }

    baseGestureCount = currentGestures.size();
    Serial.printf("[GestureEngine] All categories loaded: %d gestures total\n",
                  (int)baseGestureCount);
}

// ============================================================================
// loadGesturesForProfile()
// ============================================================================

void GestureEngine::loadGesturesForProfile(Profile profile) {
    if (profile == PROFILE_BASE) {
        // Base profile is handled by loadGesturesForCategory
        return;
    }

    std::vector<GestureDefinition> profileGestures;
    if (!loader.loadProfile(profile, profileGestures)) {
        Serial.printf("[GestureEngine] Failed to load gestures for profile %d\n",
                      static_cast<int>(profile));
        return;
    }

    // Check total gesture count limit
    size_t newTotal = currentGestures.size() + profileGestures.size();
    if (newTotal > MAX_GESTURES_TOTAL) {
        Serial.printf("[GestureEngine] Cannot load profile %d: would exceed MAX_GESTURES_TOTAL (%d + %d > %d)\n",
                      static_cast<int>(profile),
                      static_cast<int>(currentGestures.size()),
                      static_cast<int>(profileGestures.size()),
                      MAX_GESTURES_TOTAL);
        return;
    }

    // Append profile gestures after base gestures
    for (size_t i = 0; i < profileGestures.size(); i++) {
        currentGestures.push_back(profileGestures[i]);
    }

    Serial.printf("[GestureEngine] Profile %d loaded: %d gestures (total now: %d)\n",
                  static_cast<int>(profile),
                  static_cast<int>(profileGestures.size()),
                  static_cast<int>(currentGestures.size()));
}

// ============================================================================
// loadCustomGestures() — Load custom gestures from custom.json and append
// ============================================================================

void GestureEngine::loadCustomGestures() {
    std::vector<GestureDefinition> customGestures;
    if (!loader.loadCustom(customGestures)) {
        Serial.println("[GestureEngine] Failed to load custom gestures");
        return;
    }

    if (customGestures.empty()) {
        return;
    }

    // Check total gesture count limit
    size_t newTotal = currentGestures.size() + customGestures.size();
    if (newTotal > MAX_GESTURES_TOTAL) {
        Serial.printf("[GestureEngine] Cannot load custom gestures: would exceed MAX_GESTURES_TOTAL (%d + %d > %d)\n",
                      static_cast<int>(currentGestures.size()),
                      static_cast<int>(customGestures.size()),
                      MAX_GESTURES_TOTAL);
        return;
    }

    // Append custom gestures after base + profile gestures
    for (size_t i = 0; i < customGestures.size(); i++) {
        currentGestures.push_back(customGestures[i]);
    }

    Serial.printf("[GestureEngine] Custom gestures loaded: %d gestures (total now: %d)\n",
                  static_cast<int>(customGestures.size()),
                  static_cast<int>(currentGestures.size()));
}

// ============================================================================
// clearProfileGestures()
// ============================================================================

void GestureEngine::clearProfileGestures() {
    if (currentGestures.size() > baseGestureCount) {
        currentGestures.resize(baseGestureCount);
        Serial.printf("[GestureEngine] Profile gestures cleared, %d base gestures remain\n",
                      static_cast<int>(baseGestureCount));
    }
}

// ============================================================================
// updateSensorA() / updateSensorB()
// ============================================================================

void GestureEngine::updateSensorA(float ax, float ay, float az) {
    matrixA.update(ax, ay, az);
}

void GestureEngine::updateSensorAGyro(float gx, float gy, float gz) {
    // FIX INSTALL-20: Normalizar gyro de +/-GYRO_RANGE para +/-ACCEL_RANGE
    // para reutilizar o mesmo mapToGrid() da Matrix3D.
    float normGx = (gx / GYRO_RANGE) * ACCEL_RANGE;
    float normGy = (gy / GYRO_RANGE) * ACCEL_RANGE;
    float normGz = (gz / GYRO_RANGE) * ACCEL_RANGE;
    matrixAGyro.update(normGx, normGy, normGz);
}

void GestureEngine::updateSensorB(float ax, float ay, float az) {
    matrixB.update(ax, ay, az);
}

void GestureEngine::updateSensorBGyro(float gx, float gy, float gz) {
    float normGx = (gx / GYRO_RANGE) * ACCEL_RANGE;
    float normGy = (gy / GYRO_RANGE) * ACCEL_RANGE;
    float normGz = (gz / GYRO_RANGE) * ACCEL_RANGE;
    matrixBGyro.update(normGx, normGy, normGz);
}

// ============================================================================
// startRecording() / stopRecording()
// ============================================================================

void GestureEngine::startRecording() {
    matrixA.reset();
    matrixA.clearTrajectory();
    matrixB.reset();
    matrixB.clearTrajectory();
    // FIX INSTALL-20: Reset grids de gyro
    matrixAGyro.reset();
    matrixAGyro.clearTrajectory();
    matrixBGyro.reset();
    matrixBGyro.clearTrajectory();
    recordingStartMs = millis();
    recording = true;
}

void GestureEngine::setWaitingForSensorB(bool waiting) {
    _waitingForSensorB = waiting;
}

void GestureEngine::setTrainingIsContext(bool isContext) {
    _trainingIsContext = isContext;
    Serial.printf("[GestureEngine] Training sensor mode: %s (principal=%s)\n",
                  isContext ? "CONTEXTO" : "OBJETO",
                  isContext ? "Sensor A" : "Sensor B");
}

void GestureEngine::stopRecording() {
    recording = false;

    // === Modelo Orbital: extrair assinaturas ao parar gravacao ===
    // Para cada sensor: usar stroke detectado se valido (completo + duracao minima).
    // Se stroke invalido ou muito curto, usar buffer inteiro apos cooldown.
    // Isso resolve a assimetria treino (com countdown 3s) vs reconhecimento (sem countdown).

    // Sensor A accel
    if (matrixA.getRawCount() > 0) {
        size_t onA, offA;
        bool strokeOk = matrixA.isStrokeComplete() &&
            (matrixA.getStrokeOffsetIdx() - matrixA.getStrokeOnsetIdx()) >= ORBITAL_MIN_STROKE_SAMPLES;
        if (strokeOk) {
            onA = matrixA.getStrokeOnsetIdx();
            offA = matrixA.getStrokeOffsetIdx();
            DBG2("[Orbital] Stroke A detectado: onset=%d offset=%d len=%d\n",
                (int)onA, (int)offA, (int)(offA - onA));
        } else {
            // Fallback: usar buffer inteiro apos cooldown+calibracao
            onA = 20;  // STROKE_COOLDOWN_SAMPLES(15) + REST_CALIBRATION_SAMPLES(5)
            if (onA >= matrixA.getRawCount()) onA = 0;
            offA = matrixA.getRawCount();
            DBG2("[Orbital] Stroke A fallback (buffer completo): onset=%d offset=%d len=%d\n",
                (int)onA, (int)offA, (int)(offA - onA));
        }
        _capturedSigA = OrbitalExtractor::extract(
            matrixA.getRawBuffer(), matrixA.getRawCount(), onA, offA,
            matrixAGyro.getRawBuffer(), matrixAGyro.getRawCount()
        );
    }
    // Sensor A gyro
    if (matrixAGyro.getRawCount() > 0) {
        size_t onAG, offAG;
        bool strokeOkG = matrixAGyro.isStrokeComplete() &&
            (matrixAGyro.getStrokeOffsetIdx() - matrixAGyro.getStrokeOnsetIdx()) >= ORBITAL_MIN_STROKE_SAMPLES;
        if (strokeOkG) {
            onAG = matrixAGyro.getStrokeOnsetIdx();
            offAG = matrixAGyro.getStrokeOffsetIdx();
        } else {
            onAG = 20;
            if (onAG >= matrixAGyro.getRawCount()) onAG = 0;
            offAG = matrixAGyro.getRawCount();
        }
        _capturedSigAGyro = OrbitalExtractor::extract(
            matrixAGyro.getRawBuffer(), matrixAGyro.getRawCount(), onAG, offAG
        );
    }
    // Sensor B accel
    // FIX BUG-03: Fallback inteligente para deteccao de stroke do Sensor B.
    // Problema: durante reconhecimento de objetos, a gravacao inicia automaticamente
    // apos CONTEXT_WAIT (sem countdown). O usuario ja esta movendo a mao direita.
    // A calibracao de repouso (primeiras 20 amostras) captura MOVIMENTO, nao repouso.
    // Resultado: stroke detection falha, fallback usa buffer inteiro (samples 20..end),
    // incluindo repouso pos-gesto. A assinatura fica diluida (amplitude baixa,
    // duracao inflada, linearidade distorcida) e NUNCA bate com a de treino.
    //
    // Fix: quando stroke detection falha, usar as ULTIMAS amostras como referencia
    // de repouso (garantido que estao em repouso porque isStable() disparou antes
    // de stopRecording). Escanear o buffer de tras pra frente para achar onde o
    // movimento parou, e da frente pra tras para achar onde comecou.
    if (matrixB.getRawCount() > 0) {
        size_t onB, offB;
        bool strokeOkB = matrixB.isStrokeComplete() &&
            (matrixB.getStrokeOffsetIdx() - matrixB.getStrokeOnsetIdx()) >= ORBITAL_MIN_STROKE_SAMPLES;
        if (strokeOkB) {
            onB = matrixB.getStrokeOnsetIdx();
            offB = matrixB.getStrokeOffsetIdx();
            DBG2("[Orbital] Stroke B detectado: onset=%d offset=%d len=%d\n",
                (int)onB, (int)offB, (int)(offB - onB));
        } else {
            // Fallback inteligente: usar CAUDA como referencia de repouso.
            // As ultimas 10 amostras estao em repouso (isStable disparou).
            const RawSample3D* rawB = matrixB.getRawBuffer();
            size_t countB = matrixB.getRawCount();
            size_t tailStart = (countB > 10) ? (countB - 10) : 0;
            float tailMag = 0.0f;
            for (size_t i = tailStart; i < countB; i++) {
                tailMag += sqrtf(rawB[i].x * rawB[i].x +
                                 rawB[i].y * rawB[i].y +
                                 rawB[i].z * rawB[i].z);
            }
            tailMag /= (float)(countB - tailStart);

            // Escanear de tras pra frente: achar onde magnitude desvia do repouso
            // (fim do gesto). Usar threshold suave (0.15g = ORBITAL_OFFSET_THRESHOLD_G).
            offB = countB;
            for (size_t i = countB; i > 0; i--) {
                float mag = sqrtf(rawB[i-1].x * rawB[i-1].x +
                                  rawB[i-1].y * rawB[i-1].y +
                                  rawB[i-1].z * rawB[i-1].z);
                if (fabsf(mag - tailMag) > ORBITAL_OFFSET_THRESHOLD_G) {
                    offB = i;  // i e o indice APOS a ultima amostra com movimento
                    break;
                }
            }

            // Escanear da frente pra tras: achar onde magnitude desvia do repouso
            // (inicio do gesto). Pular cooldown (primeiras amostras podem ter ruido).
            size_t scanStart = 5;  // Pular primeiras 5 amostras (100ms de ruido)
            if (scanStart >= countB) scanStart = 0;
            onB = scanStart;
            for (size_t i = scanStart; i < offB; i++) {
                float mag = sqrtf(rawB[i].x * rawB[i].x +
                                  rawB[i].y * rawB[i].y +
                                  rawB[i].z * rawB[i].z);
                if (fabsf(mag - tailMag) > ORBITAL_ONSET_THRESHOLD_G) {
                    // Onset comecou algumas amostras antes da primeira deteccao
                    onB = (i > 3) ? (i - 3) : 0;
                    break;
                }
            }

            // Garantir minimo de 3 amostras para extract() funcionar
            if (offB <= onB || (offB - onB) < 3) {
                // Ultimo recurso: usar buffer inteiro apos 5 amostras
                onB = 5;
                if (onB >= countB) onB = 0;
                offB = countB;
            }

            DBG2("[Orbital] Stroke B fallback smart: onset=%d offset=%d len=%d (restMag=%.2f)\n",
                (int)onB, (int)offB, (int)(offB - onB), tailMag);
        }
        _capturedSigB = OrbitalExtractor::extract(
            matrixB.getRawBuffer(), matrixB.getRawCount(), onB, offB,
            matrixBGyro.getRawBuffer(), matrixBGyro.getRawCount()
        );
    }
    // FIX BUG-02: Extrair assinatura gyro do Sensor B
    // Sem isso, o score gyro de objetos usava Sensor A (contaminado pelo contexto).
    // FIX BUG-03: Mesma logica de fallback inteligente do accel (ver acima).
    if (matrixBGyro.getRawCount() > 0) {
        size_t onBG, offBG;
        bool strokeOkBG = matrixBGyro.isStrokeComplete() &&
            (matrixBGyro.getStrokeOffsetIdx() - matrixBGyro.getStrokeOnsetIdx()) >= ORBITAL_MIN_STROKE_SAMPLES;
        if (strokeOkBG) {
            onBG = matrixBGyro.getStrokeOnsetIdx();
            offBG = matrixBGyro.getStrokeOffsetIdx();
        } else {
            // Fallback inteligente: mesma logica do accel B (ver FIX BUG-03 acima)
            const RawSample3D* rawBG = matrixBGyro.getRawBuffer();
            size_t countBG = matrixBGyro.getRawCount();
            size_t tailStartG = (countBG > 10) ? (countBG - 10) : 0;
            float tailMagG = 0.0f;
            for (size_t i = tailStartG; i < countBG; i++) {
                tailMagG += sqrtf(rawBG[i].x * rawBG[i].x +
                                  rawBG[i].y * rawBG[i].y +
                                  rawBG[i].z * rawBG[i].z);
            }
            tailMagG /= (float)(countBG - tailStartG);

            offBG = countBG;
            for (size_t i = countBG; i > 0; i--) {
                float mag = sqrtf(rawBG[i-1].x * rawBG[i-1].x +
                                  rawBG[i-1].y * rawBG[i-1].y +
                                  rawBG[i-1].z * rawBG[i-1].z);
                if (fabsf(mag - tailMagG) > ORBITAL_OFFSET_THRESHOLD_G) {
                    offBG = i;
                    break;
                }
            }

            size_t scanStartG = 5;
            if (scanStartG >= countBG) scanStartG = 0;
            onBG = scanStartG;
            for (size_t i = scanStartG; i < offBG; i++) {
                float mag = sqrtf(rawBG[i].x * rawBG[i].x +
                                  rawBG[i].y * rawBG[i].y +
                                  rawBG[i].z * rawBG[i].z);
                if (fabsf(mag - tailMagG) > ORBITAL_ONSET_THRESHOLD_G) {
                    onBG = (i > 3) ? (i - 3) : 0;
                    break;
                }
            }

            if (offBG <= onBG || (offBG - onBG) < 3) {
                onBG = 5;
                if (onBG >= countBG) onBG = 0;
                offBG = countBG;
            }
        }
        _capturedSigBGyro = OrbitalExtractor::extract(
            matrixBGyro.getRawBuffer(), matrixBGyro.getRawCount(), onBG, offBG
        );
    }

    // Log para debug — SigB sempre visivel (critico para diagnostico de objetos)
    if (_capturedSigB.valid) {
        Serial.printf("[Orbital] SigB: amp=%.2f peak=%.2f lin=%.2f dur=%.0f sm=%.2f rot=%.2f sym=%.2f valid=%d\n",
            _capturedSigB.amplitude, _capturedSigB.peak, _capturedSigB.linearity,
            _capturedSigB.duration, _capturedSigB.smoothness,
            _capturedSigB.rotation, _capturedSigB.symmetry, _capturedSigB.valid);
    } else {
        Serial.printf("[Orbital] SigB: INVALID (rawCount=%d)\n", (int)matrixB.getRawCount());
    }

    if (_capturedSigA.valid) {
        DBG2("[Orbital] SigA: amp=%.2f peak=%.2f lin=%.2f dur=%.0f sm=%.2f rot=%.2f sym=%.2f\n",
            _capturedSigA.amplitude, _capturedSigA.peak, _capturedSigA.linearity,
            _capturedSigA.duration, _capturedSigA.smoothness,
            _capturedSigA.rotation, _capturedSigA.symmetry);
        DBG2("[Orbital] SigA plane: [%.2f, %.2f, %.2f]\n",
            _capturedSigA.planeNormal[0], _capturedSigA.planeNormal[1], _capturedSigA.planeNormal[2]);
    }
}

// ============================================================================
// isGestureComplete()
// ============================================================================

bool GestureEngine::isGestureComplete() const {
    if (!recording) {
        return false;
    }

    // Condition 1: Minimum recording duration elapsed (from level config)
    unsigned long elapsed = millis() - recordingStartMs;
    if (elapsed < _levelConfig.minRecordingMs) {
        return false;
    }

    // FIX FLOW-04: Cada gesto usa 1 sensor.
    // Dispositivo com HAT (Sensor A, mao esquerda) = contextos
    // Dispositivo 2 (Sensor B, mao direita) = objetos

    bool aReady = (static_cast<int>(matrixA.getTrajectory().size()) >= _levelConfig.minTrajectoryLen)
                  && matrixA.hasMoved()
                  && (matrixA.getTotalMovement() >= MIN_GESTURE_MOVEMENT)
                  && matrixA.isStable(_levelConfig.stableDurationMs);

    // FIX FLOW-08: Threshold do Sensor B. 1.0 era muito baixo — aceitava
    // vibracao do speaker e micro-tremor como gesto. 2.0 filtra ruido mas
    // aceita gestos reais (dados reais: movement=2.0-5.0 em gestos validos).
    static constexpr float MIN_GESTURE_MOVEMENT_B = 2.0f;
    bool bReady = (static_cast<int>(matrixB.getTrajectory().size()) >= _levelConfig.minTrajectoryLen)
                  && matrixB.hasMoved()
                  && (matrixB.getTotalMovement() >= MIN_GESTURE_MOVEMENT_B)
                  && matrixB.isStable(_levelConfig.stableDurationMs);

    // Se esperando Sensor B (segundo gesto da frase composta),
    // so completa quando Sensor B tiver movimento. Sensor A (double-tap) e ignorado.
    if (_waitingForSensorB) {
        return bReady;
    }

    return aReady || bReady;
}

// ============================================================================
// detectDominantHand()
// ============================================================================

HandDominance GestureEngine::detectDominantHand() const {
    float movA = matrixA.getTotalMovement();
    float movB = matrixB.getTotalMovement();

    // Both hands near zero — no meaningful gesture detected
    if (movA < 1.0f && movB < 1.0f) {
        return HAND_UNKNOWN;
    }

    // Left hand (sensor B) significantly more active
    if (movB > movA * 1.5f) {
        return HAND_LEFT;
    }

    // Right hand (sensor A) significantly more active
    if (movA > movB * 1.5f) {
        return HAND_RIGHT;
    }

    // Both hands similar movement
    return HAND_BOTH;
}

// ============================================================================
// processGesture()
// ============================================================================

GestureResult GestureEngine::processGesture() {
    GestureResult result;
    memset(&result, 0, sizeof(GestureResult));
    result.automationCmd = CMD_NONE;
    result.contextIndex = -1;
    result.isContext = false;
    result.handDominance = HAND_RIGHT;  // Sensor A = sempre mao direita (objetos)

    // === REJECTION RAPIDA ===
    // Rejeitar capturas que nao podem ser gestos validos ANTES de gastar CPU no matching.
    // Previne falsos positivos por vibracao do beep, micro-tremor ou double-tap residual.
    {
        float movA = matrixA.getTotalMovement();
        float movB = matrixB.getTotalMovement();
        unsigned long duration = millis() - recordingStartMs;
        size_t ptsA = matrixA.getTrajectory().size();
        size_t ptsB = matrixB.getTrajectory().size();

        // Amplitude minima: se nenhum sensor teve movimento significativo, rejeitar.
        // Vibracao do beep/double-tap gera ~1-3 de movement. Gestos reais geram ~5+.
        static constexpr float MIN_TOTAL_MOVEMENT = 4.0f;
        if (movA < MIN_TOTAL_MOVEMENT && movB < MIN_TOTAL_MOVEMENT) {
            Serial.printf("[REJECT] Amplitude insuficiente: movA=%.1f movB=%.1f (min=%.1f)\n",
                          movA, movB, MIN_TOTAL_MOVEMENT);
            result.matched = false;
            return result;
        }

        // Trajetoria minima: precisa de pelo menos 3 pontos em algum sensor
        if (ptsA < 3 && ptsB < 3) {
            Serial.printf("[REJECT] Trajetoria curta: ptsA=%d ptsB=%d\n", (int)ptsA, (int)ptsB);
            result.matched = false;
            return result;
        }

        Serial.printf("[PRE-MATCH] movA=%.1f ptsA=%d movB=%.1f ptsB=%d dur=%lums\n",
                      movA, (int)ptsA, movB, (int)ptsB, duration);
    }

    // Converte trajetorias capturadas de deque para vector (DTW requer vector)
    const std::deque<Point3D>& trajADeque = matrixA.getTrajectory();
    const std::deque<Point3D>& trajBDeque = matrixB.getTrajectory();
    std::vector<Point3D> trajA;
    trajA.reserve(trajADeque.size());
    trajA.assign(trajADeque.begin(), trajADeque.end());
    std::vector<Point3D> trajB;
    trajB.reserve(trajBDeque.size());
    trajB.assign(trajBDeque.begin(), trajBDeque.end());

    // FIX FLOW-04: Dispositivo com HAT (Sensor A) = mao esquerda = contextos.
    // Verificar se Sensor A teve movimento para tentar match de contexto.
    bool sensorAActive = matrixA.hasMoved() &&
                         matrixA.getTotalMovement() >= MIN_GESTURE_MOVEMENT;

    // Dispositivo 2 (Sensor B) = mao direita = objetos.
    bool sensorBActive = matrixB.hasMoved() &&
                         matrixB.getTotalMovement() >= 2.0f;

    DBG2("[Orbital] SensorA: mov=%.2f active=%s | SensorB: mov=%.2f active=%s\n",
                  matrixA.getTotalMovement(), sensorAActive ? "Y" : "N",
                  matrixB.getTotalMovement(), sensorBActive ? "Y" : "N");

    // No gestures loaded
    if (currentGestures.empty()) {
        result.matched = false;
        return result;
    }

    // FIX FLOW-04: Contextos sao do Sensor A (HAT, mao esquerda).
    // Se Sensor A teve movimento → tentar match em contextos primeiro.
    if (sensorAActive && !contexts.empty()) {
        // Filtra apenas gestos de contexto e faz match com trajectory_b
        std::vector<std::vector<Point3D>> contextGestA;
        std::vector<std::vector<Point3D>> contextGestB;
        std::vector<float> contextThresholds;
        std::vector<size_t> contextIndices;

        for (size_t i = 0; i < currentGestures.size(); i++) {
            // FIX INSTALL-17: So contextos treinados
            if (currentGestures[i].isContext && currentGestures[i].trained) {
                contextGestA.push_back(currentGestures[i].trajectoryA);
                contextGestB.push_back(currentGestures[i].trajectoryB);
                contextThresholds.push_back(currentGestures[i].threshold * _levelConfig.dtwThresholdMultiplier);
                contextIndices.push_back(i);
            }
        }

        if (!contextGestA.empty()) {
            // Modelo Orbital: contextos — compara assinatura orbital do sensor B
            float bestCtxScore = 0.0f;
            int bestCtxIndex = -1;

            // Ratio test: guardar best e second best para rejeitar ambiguos
            float secondBestCtxScore = 0.0f;
            for (size_t i = 0; i < contextIndices.size(); i++) {
                const GestureDefinition& ctxGesture = currentGestures[contextIndices[i]];
                float score;
                // FIX FLOW-04: Contextos sao do Sensor A (HAT, mao esquerda).
                if (_capturedSigA.valid && ctxGesture.signatureA.valid) {
                    float dist = OrbitalExtractor::distance(_capturedSigA, ctxGesture.signatureA);
                    score = OrbitalExtractor::distanceToScore(dist);
                } else {
                    score = sequenceSimilarity(trajADeque, contextGestA[i]);
                }
                if (score > bestCtxScore) {
                    secondBestCtxScore = bestCtxScore;
                    bestCtxScore = score;
                    bestCtxIndex = static_cast<int>(i);
                } else if (score > secondBestCtxScore) {
                    secondBestCtxScore = score;
                }
            }

            // Threshold por gesto: usar orbitalThreshold do gesto se disponivel
            float ctxThreshold = ORBITAL_MATCH_THRESHOLD;
            if (bestCtxIndex >= 0) {
                const GestureDefinition& bestCtx = currentGestures[contextIndices[bestCtxIndex]];
                if (bestCtx.orbitalThreshold > 0.0f) {
                    ctxThreshold = bestCtx.orbitalThreshold;
                }
            }

            // Ratio test (Lowe, 2004): rejeitar se best e second sao muito proximos.
            // Ratio alto = ambiguo. 0.95 = aceita quando best e 5%+ melhor que second.
            bool ctxAmbiguous = (bestCtxScore > 0.01f && secondBestCtxScore > 0.01f &&
                                 (secondBestCtxScore / bestCtxScore) > 0.95f);

            if (bestCtxIndex >= 0 && bestCtxScore >= ctxThreshold && !ctxAmbiguous) {
                size_t originalIdx = contextIndices[bestCtxIndex];
                const GestureDefinition& gesture = currentGestures[originalIdx];

                result.matched = true;
                result.gestureIndex = static_cast<int>(originalIdx);
                result.confidence = bestCtxScore;
                result.isContext = true;
                result.handDominance = HAND_LEFT;
                result.ambiguous = false;

                if (gesture.isContext) {
                    uint8_t ctxId = gesture.id & 0xFF;
                    for (int ci = 0; ci < static_cast<int>(contexts.size()); ci++) {
                        if (contexts[ci].id == ctxId) {
                            result.contextIndex = ci;
                            break;
                        }
                    }
                }

                strncpy(result.gestureName, gesture.name, sizeof(result.gestureName) - 1);
                result.gestureName[sizeof(result.gestureName) - 1] = '\0';
                strncpy(result.audioFile, gesture.audioFile, sizeof(result.audioFile) - 1);
                result.audioFile[sizeof(result.audioFile) - 1] = '\0';

                DBG1("[SEQ] Context MATCH: %s (score=%.0f%%)\n",
                              gesture.name, bestCtxScore * 100.0f);
                return result;
            }
        }
    }

    // Se Sensor A teve movimento significativo mas NENHUM contexto bateu,
    // NAO tentar objetos — o gesto da mao esquerda nao e objeto.
    // Objetos so sao testados quando:
    // 1. waitingForSensorB = true (segundo gesto da frase composta)
    // 2. Sensor A NAO teve movimento (gesto foi so da mao direita, solo)
    if (sensorAActive && !_waitingForSensorB) {
        Serial.println("[MATCH] Sensor A ativo mas sem contexto — nao testar objetos");
        result.matched = false;
        return result;
    }

    // Match objetos — so chega aqui se waitingForSensorB ou se Sensor A estava parado
    std::vector<std::vector<Point3D>> objectGestA;
    std::vector<std::vector<Point3D>> objectGestB;
    std::vector<float> objectThresholds;
    std::vector<size_t> objectIndices;

    for (size_t i = 0; i < currentGestures.size(); i++) {
        // FIX INSTALL-17: Ignorar gestos nao treinados (placeholders).
        // Placeholders tem trajetorias genericas que fazem match com qualquer movimento.
        // So inclui gestos com trained=true (treinados pelo usuario).
        if (!currentGestures[i].isContext && currentGestures[i].trained) {
            objectGestA.push_back(currentGestures[i].trajectoryA);
            objectGestB.push_back(currentGestures[i].trajectoryB);
            objectThresholds.push_back(currentGestures[i].threshold * _levelConfig.dtwThresholdMultiplier);
            objectIndices.push_back(i);
        }
    }

    if (objectGestA.empty()) {
        result.matched = false;
        return result;
    }

    // === FIX FLOW-04: Objetos sao do Sensor B (Dispositivo 2, mao direita). ===
    float bestScore = 0.0f;
    float secondBestScore = 0.0f;  // Para ratio test (Lowe, 2004)
    int bestIndex = -1;

    for (size_t i = 0; i < objectIndices.size(); i++) {
        const GestureDefinition& gesture = currentGestures[objectIndices[i]];
        float scoreAccel = 0.0f;
        float scoreGyro = 1.0f;

        // Sensor B (Dispositivo 2, mao direita) = sensor primario para objetos
        float scoreSensorB = 0.0f;
        if (_capturedSigB.valid && gesture.signatureB.valid) {
            float distB = OrbitalExtractor::distance(_capturedSigB, gesture.signatureB);
            scoreSensorB = OrbitalExtractor::distanceToScore(distB);
        } else if (!trajBDeque.empty() && !objectGestB[i].empty()) {
            scoreSensorB = sequenceSimilarity(trajBDeque, objectGestB[i]);
        }
        // Debug minimo — so para o MELHOR match (fora do loop)
        // Movido para apos o loop para nao encher o TX buffer

        // Sensor A (HAT, mao esquerda) = fallback para objetos
        // (compatibilidade com gestos treinados antes desta correcao)
        float scoreSensorA = 0.0f;
        if (_capturedSigA.valid && gesture.signatureA.valid) {
            float distA = OrbitalExtractor::distance(_capturedSigA, gesture.signatureA);
            scoreSensorA = OrbitalExtractor::distanceToScore(distA);
        } else if (!trajADeque.empty() && !objectGestA[i].empty()) {
            scoreSensorA = sequenceSimilarity(trajADeque, objectGestA[i]);
        }

        // FIX FLOW-09 + FIX BUG-01: Objetos usam APENAS Sensor B (mao direita).
        // O fallback do Sensor A contaminava o match — a assinatura residual
        // do contexto ("I want") fazia match errado com objetos.
        // BUG-01: Fallback removido. Se Sensor B nao tem dados, score fica 0
        // (gesto nao reconhecido e melhor que gesto errado).
        scoreAccel = scoreSensorB;

        // FIX BUG-02: Score gyro — usar sensor correspondente ao tipo de gesto.
        // Contextos = Sensor A gyro, Objetos = Sensor B gyro.
        if (gesture.isContext) {
            // Contexto: gyro do Sensor A (mao esquerda)
            if (_capturedSigAGyro.valid && gesture.signatureAGyro.valid) {
                float distG = OrbitalExtractor::distance(_capturedSigAGyro, gesture.signatureAGyro);
                scoreGyro = OrbitalExtractor::distanceToScore(distG);
            } else if (!gesture.trajectoryAGyro.empty()) {
                const std::deque<Point3D>& gyroADeque = matrixAGyro.getTrajectory();
                if (gyroADeque.size() > 1) {
                    scoreGyro = sequenceSimilarity(gyroADeque, gesture.trajectoryAGyro);
                }
            }
        } else {
            // Objeto: gyro do Sensor B (mao direita)
            if (_capturedSigBGyro.valid && gesture.signatureAGyro.valid) {
                float distG = OrbitalExtractor::distance(_capturedSigBGyro, gesture.signatureAGyro);
                scoreGyro = OrbitalExtractor::distanceToScore(distG);
            } else if (!gesture.trajectoryAGyro.empty()) {
                const std::deque<Point3D>& gyroBDeque = matrixBGyro.getTrajectory();
                if (gyroBDeque.size() > 1) {
                    scoreGyro = sequenceSimilarity(gyroBDeque, gesture.trajectoryAGyro);
                }
            }
        }

        // Score combinado: 70% accel + 30% gyro
        float combined = 0.7f * scoreAccel + 0.3f * scoreGyro;

        DBG2("[Orbital] %s: sA=%.0f%% sB=%.0f%% gyro=%.0f%% comb=%.0f%% (min: %.0f%%)\n",
                      gesture.name,
                      scoreSensorA * 100.0f, scoreSensorB * 100.0f,
                      scoreGyro * 100.0f,
                      combined * 100.0f, ORBITAL_MATCH_THRESHOLD * 100.0f);

        if (combined > bestScore) {
            secondBestScore = bestScore;
            bestScore = combined;
            bestIndex = static_cast<int>(i);
        } else if (combined > secondBestScore) {
            secondBestScore = combined;
        }
    }

    // Threshold por gesto: usar orbitalThreshold do gesto se disponivel
    float objThreshold = ORBITAL_MATCH_THRESHOLD;
    if (bestIndex >= 0) {
        const GestureDefinition& bestGesture = currentGestures[objectIndices[bestIndex]];
        if (bestGesture.orbitalThreshold > 0.0f) {
            objThreshold = bestGesture.orbitalThreshold;
        }
    }

    // Ratio test (Lowe, 2004): rejeitar se best e second sao muito proximos.
    // 0.85 era muito restritivo com poucos gestos treinados (water e food
    // geravam scores parecidos: 60% vs 55% = ratio 0.92 → rejeitado).
    // 0.95 aceita quando um e claramente melhor (5%+ de diferenca).
    bool objAmbiguous = (bestScore > 0.01f && secondBestScore > 0.01f &&
                         (secondBestScore / bestScore) > 0.95f);

    if (objAmbiguous) {
        DBG2("[Orbital] REJEITADO: ambiguo (best=%.0f%% second=%.0f%% ratio=%.2f)\n",
                      bestScore * 100.0f, secondBestScore * 100.0f,
                      secondBestScore / bestScore);
    }

    // Aceitar somente se score >= threshold E nao ambiguo
    if (bestIndex >= 0 && bestScore >= objThreshold && !objAmbiguous) {
        size_t originalIdx = objectIndices[bestIndex];
        const GestureDefinition& gesture = currentGestures[originalIdx];

        result.matched = true;
        result.gestureIndex = static_cast<int>(originalIdx);
        result.confidence = bestScore;
        result.isSolo = gesture.isSolo;
        result.isContext = gesture.isContext;
        result.hasAutomation = (gesture.automationCmd != CMD_NONE);
        result.automationCmd = gesture.automationCmd;
        result.ambiguous = objAmbiguous;

        strncpy(result.gestureName, gesture.name, sizeof(result.gestureName) - 1);
        result.gestureName[sizeof(result.gestureName) - 1] = '\0';

        strncpy(result.audioFile, gesture.audioFile, sizeof(result.audioFile) - 1);
        result.audioFile[sizeof(result.audioFile) - 1] = '\0';

        Serial.printf("[Orbital] OBJ MATCH: %s (score=%.0f%% thresh=%.0f%%)\n",
                      gesture.name, bestScore * 100.0f, objThreshold * 100.0f);
    } else {
        result.matched = false;
        // FIX BUG-03: Log sempre visivel para diagnostico de objetos
        Serial.printf("[Orbital] OBJ NO MATCH — best=%.0f%%, threshold=%.0f%%, sigB=%d, gestSigB=%d\n",
                      bestScore * 100.0f, objThreshold * 100.0f,
                      _capturedSigB.valid ? 1 : 0,
                      (objectIndices.size() > 0 && bestIndex >= 0) ?
                          (currentGestures[objectIndices[bestIndex]].signatureB.valid ? 1 : 0) : -1);
    }

    return result;
}

// ============================================================================
// checkAutomationGestures()
// ============================================================================

AutomationResult GestureEngine::checkAutomationGestures() {
    AutomationResult result;
    result.triggered = false;
    result.command = CMD_NONE;

    if (!_ready) {
        return result;
    }

    unsigned long now = millis();

    // FIX M08: Cooldown com subtracao (seguro contra overflow de millis).
    // Antes: comparacao direta "now < deadline" quebra apos ~49.7 dias
    // de uptime quando millis() faz wraparound para zero.
    // Subtracao unsigned: (now - triggerTime) funciona mesmo com overflow.
    if (automationCooldownMs > 0 && (now - automationCooldownMs) < AUTOMATION_COOLDOWN_MS) {
        return result;
    }

    // Only check at the defined interval to save CPU
    if (now - lastAutomationCheckMs < AUTOMATION_CHECK_INTERVAL_MS) {
        return result;
    }
    lastAutomationCheckMs = now;

    // Check if main matrix has enough movement
    if (!matrixA.hasMovement()) {
        return result;
    }

    // Get current trajectories from the main matrices (deque) and convert to vector for DTW
    const std::deque<Point3D>& trajADeque = matrixA.getTrajectory();
    const std::deque<Point3D>& trajBDeque = matrixB.getTrajectory();
    std::vector<Point3D> trajA(trajADeque.begin(), trajADeque.end());
    std::vector<Point3D> trajB(trajBDeque.begin(), trajBDeque.end());

    // Minimum trajectory length check
    if (static_cast<int>(trajA.size()) < DTW_MIN_TRAJECTORY_LEN) {
        return result;
    }

    // Build reference vectors ONLY from automation gestures
    // (gestures with automation_cmd != CMD_NONE and empty audioFile)
    std::vector<std::vector<Point3D>> gesturesA;
    std::vector<std::vector<Point3D>> gesturesB;
    std::vector<float> thresholds;
    std::vector<size_t> automationIndices;  // Map back to currentGestures index

    for (size_t i = 0; i < currentGestures.size(); i++) {
        // FIX INSTALL-17: So gestos treinados de automacao
        if (currentGestures[i].automationCmd != CMD_NONE &&
            strlen(currentGestures[i].audioFile) == 0 &&
            currentGestures[i].trained) {
            gesturesA.push_back(currentGestures[i].trajectoryA);
            gesturesB.push_back(currentGestures[i].trajectoryB);
            thresholds.push_back(currentGestures[i].threshold);
            automationIndices.push_back(i);
        }
    }

    // No automation-only gestures loaded
    if (gesturesA.empty()) {
        return result;
    }

    // Execute DTW matching against automation gestures only
    DTWResult dtwResult = dtw.match(trajA, trajB, gesturesA, gesturesB, thresholds);

    if (dtwResult.matched) {
        size_t originalIdx = automationIndices[dtwResult.matchIndex];
        const GestureDefinition& gesture = currentGestures[originalIdx];

        result.triggered = true;
        result.command = gesture.automationCmd;

        // FIX M08: Salva timestamp (nao deadline) para subtracao segura
        automationCooldownMs = now;

        Serial.printf("[GestureEngine] Automation gesture triggered: %s (cmd: 0x%02X)\n",
                      gesture.name, static_cast<int>(gesture.automationCmd));
    }

    return result;
}

// ============================================================================
// loadContextGestures() — Load context definitions and convert to gestures
// ============================================================================

void GestureEngine::loadContextGestures() {
    contexts.clear();

    if (!loader.loadContexts(contexts)) {
        Serial.println("[GestureEngine] No context gestures loaded (file missing or parse error)");
        return;
    }

    if (contexts.empty()) {
        return;
    }

    // Convert each ContextDefinition into a GestureDefinition and append
    // to currentGestures so they participate in DTW matching.
    for (size_t i = 0; i < contexts.size(); i++) {
        // Check total gesture count limit
        if (currentGestures.size() >= MAX_GESTURES_TOTAL) {
            Serial.printf("[GestureEngine] Cannot load more contexts: MAX_GESTURES_TOTAL reached (%d)\n",
                          MAX_GESTURES_TOTAL);
            break;
        }

        GestureDefinition gd;
        gd.id = 0xE000 | contexts[i].id;  // 0xE0xx ID range for contexts
        // Sprint C2: idStr formato "CXnn" (CX = Context). Necessario pro menu local
        // chamar startTrainingMode("CXnn",...) e o save mapear pra contexts.json.
        snprintf(gd.idStr, sizeof(gd.idStr), "CX%02u", static_cast<unsigned>(contexts[i].id));
        strncpy(gd.name, contexts[i].prefix, sizeof(gd.name) - 1);
        gd.name[sizeof(gd.name) - 1] = '\0';
        gd.category = currentCategory;
        strncpy(gd.audioFile, contexts[i].audioFile, sizeof(gd.audioFile) - 1);
        gd.audioFile[sizeof(gd.audioFile) - 1] = '\0';
        gd.threshold = DTW_THRESHOLD_DEFAULT;
        gd.isSolo = true;   // Context gestures use sensor A (left hand) only
        gd.isContext = true;
        gd.automationCmd = CMD_NONE;
        gd.trajectoryA = contexts[i].trajectory;  // Context trajectory mapped to sensor A
        // trajectoryB remains empty (solo gesture)
        // Marcar como treinado SOMENTE se tem assinatura orbital valida.
        // Placeholder trajectories (todos os contextos tem) nao contam como treinado.
        // Antes: !trajectory.empty() marcava TODOS como trained (bug — "Call" aparecia).
        gd.trained = contexts[i].signature.valid;
        gd.signatureA = contexts[i].signature;  // Assinatura orbital do contexto (se carregada)
        gd.durationMs = 0;

        currentGestures.push_back(gd);
    }

    Serial.printf("[GestureEngine] Context gestures loaded: %d contexts (total gestures now: %d)\n",
                  static_cast<int>(contexts.size()),
                  static_cast<int>(currentGestures.size()));
}

// ============================================================================
// matchContext() — Match against loaded context definitions
// ============================================================================

int GestureEngine::matchContext() {
    // Not used in current flow (contexts are matched via processGesture),
    // but available for future direct context matching.
    return -1;
}

// ============================================================================
// getContext() / getContextCount()
// ============================================================================

const ContextDefinition& GestureEngine::getContext(int index) const {
    static const ContextDefinition emptyContext = {};
    if (index < 0 || index >= static_cast<int>(contexts.size())) {
        return emptyContext;
    }
    return contexts[index];
}

int GestureEngine::getContextCount() const {
    return static_cast<int>(contexts.size());
}

// ============================================================================
// Accessors
// ============================================================================

bool GestureEngine::isRecording() const {
    return recording;
}

int GestureEngine::getGestureCount() const {
    return static_cast<int>(currentGestures.size());
}

const GestureDefinition& GestureEngine::getGesture(int index) const {
    // Bounds check: return static empty definition if index out of range
    static const GestureDefinition emptyGesture = {};
    if (index < 0 || index >= static_cast<int>(currentGestures.size())) {
        return emptyGesture;
    }
    return currentGestures[index];
}

Category GestureEngine::getCurrentCategory() const {
    return currentCategory;
}

bool GestureEngine::isReady() const {
    return _ready;
}

GestureLoader& GestureEngine::getLoader() {
    return loader;
}

// ============================================================================
// jaccardSimilarity() — Comparacao deterministica de conjuntos de celulas
// ============================================================================

float GestureEngine::jaccardSimilarity(const std::deque<Point3D>& captured,
                                        const std::vector<Point3D>& reference) {
    // Converter trajetorias em conjuntos de celulas unicas.
    // FIX ALT-14: Encoding alinhado com grid 7x7x7 (GRID_SIZE).
    // key = x * GRID_SIZE^2 + y * GRID_SIZE + z (unico para qualquer GRID_SIZE)
    const uint16_t GS2 = GRID_SIZE * GRID_SIZE;  // 49 para grid 7x7x7

    // Conjunto A: celulas do gesto capturado
    std::vector<uint16_t> setA;
    setA.reserve(captured.size());
    for (const auto& p : captured) {
        uint16_t key = static_cast<uint16_t>(p.x) * GS2 +
                       static_cast<uint16_t>(p.y) * GRID_SIZE +
                       static_cast<uint16_t>(p.z);
        // Adicionar se nao existe (simula set)
        bool found = false;
        for (size_t i = 0; i < setA.size(); i++) {
            if (setA[i] == key) { found = true; break; }
        }
        if (!found) setA.push_back(key);
    }

    // Conjunto B: celulas do gesto de referencia
    std::vector<uint16_t> setB;
    setB.reserve(reference.size());
    for (const auto& p : reference) {
        uint16_t key = static_cast<uint16_t>(p.x) * GS2 +
                       static_cast<uint16_t>(p.y) * GRID_SIZE +
                       static_cast<uint16_t>(p.z);
        bool found = false;
        for (size_t i = 0; i < setB.size(); i++) {
            if (setB[i] == key) { found = true; break; }
        }
        if (!found) setB.push_back(key);
    }

    // Calcular intersecao |A ∩ B|
    int intersection = 0;
    for (size_t i = 0; i < setA.size(); i++) {
        for (size_t j = 0; j < setB.size(); j++) {
            if (setA[i] == setB[j]) {
                intersection++;
                break;
            }
        }
    }

    // Uniao |A ∪ B| = |A| + |B| - |A ∩ B|
    int unionSize = static_cast<int>(setA.size()) + static_cast<int>(setB.size()) - intersection;

    if (unionSize == 0) return 0.0f;

    return static_cast<float>(intersection) / static_cast<float>(unionSize);
}

// ============================================================================
// sequenceSimilarity() — Comparacao por sequencia ordenada de celulas
// ============================================================================

float GestureEngine::sequenceSimilarity(const std::deque<Point3D>& captured,
                                         const std::vector<Point3D>& reference) {
    if (captured.empty() || reference.empty()) {
        return 0.0f;
    }

    // Usa a trajetoria MAIOR como base de comparacao.
    // Mapeia cada ponto da base para o ponto correspondente da outra
    // via indice proporcional (nearest neighbor interpolation).
    size_t baseLen = std::max(captured.size(), reference.size());
    size_t otherLen = std::min(captured.size(), reference.size());
    bool capturedIsBase = (captured.size() >= reference.size());

    int matchCount = 0;

    for (size_t i = 0; i < baseLen; i++) {
        // Indice proporcional na outra trajetoria
        size_t otherIdx = (i * otherLen) / baseLen;
        if (otherIdx >= otherLen) otherIdx = otherLen - 1;

        // Ponto da base e ponto correspondente da outra
        const Point3D& basePoint = capturedIsBase ? captured[i] : reference[i];
        const Point3D& otherPoint = capturedIsBase ? reference[otherIdx] : captured[otherIdx];

        // FIX INSTALL-20: Tolerancia ±1 celula (vizinhanca).
        // Match exato era muito rigido (14% max). ±1 com threshold 70%
        // combinado com 6D (accel+gyro) filtra falsos positivos.
        int dx = abs(static_cast<int>(basePoint.x) - static_cast<int>(otherPoint.x));
        int dy = abs(static_cast<int>(basePoint.y) - static_cast<int>(otherPoint.y));
        int dz = abs(static_cast<int>(basePoint.z) - static_cast<int>(otherPoint.z));

        if (dx <= 1 && dy <= 1 && dz <= 1) {
            matchCount++;
        }
    }

    return static_cast<float>(matchCount) / static_cast<float>(baseLen);
}

// ============================================================================
// Diagnostic Accessors
// ============================================================================

const Matrix3D& GestureEngine::getMatrixA() const {
    return matrixA;
}

const Matrix3D& GestureEngine::getMatrixAGyro() const {
    return matrixAGyro;
}

const Matrix3D& GestureEngine::getMatrixB() const {
    return matrixB;
}

const LevelConfig& GestureEngine::getLevelConfig() const {
    return _levelConfig;
}

void GestureEngine::setLevelConfig(const LevelConfig& config) {
    _levelConfig = config;
    Serial.printf("[GestureEngine] Level config applied: minRec=%dms, timeout=%dms, stable=%dms, dtwMul=%.2f, tapThr=%.1f, minTraj=%d\n",
                  config.minRecordingMs, config.gestureTimeoutMs, config.stableDurationMs,
                  config.dtwThresholdMultiplier, config.doubleTapThreshold, config.minTrajectoryLen);
}

// ============================================================================
// Training Mode Methods
// ============================================================================

void GestureEngine::startTrainingSample() {
    // Reuse existing recording infrastructure
    startRecording();
}

void GestureEngine::stopTrainingSample() {
    stopRecording();
}

bool GestureEngine::addTrainingSample() {
    if (static_cast<int>(_trainingSamplesA.size()) >= TRAINING_SAMPLES_NEEDED) {
        Serial.println("[GestureEngine] Training buffer full, cannot add more samples");
        return false;
    }

    // Copy current trajectories (deque -> vector) into training buffers
    const std::deque<Point3D>& trajADeque = matrixA.getTrajectory();
    const std::deque<Point3D>& trajBDeque = matrixB.getTrajectory();

    std::vector<Point3D> sampleA(trajADeque.begin(), trajADeque.end());
    std::vector<Point3D> sampleB(trajBDeque.begin(), trajBDeque.end());

    // FIX TRAIN-SENSOR: Validar SOMENTE o sensor principal do tipo de gesto.
    // Contextos (CXxx) = Sensor A e o principal, B deve estar vazio.
    // Objetos (Gxx) = Sensor B e o principal, A deve estar vazio.
    int trajLenA = static_cast<int>(sampleA.size());
    int trajLenB = static_cast<int>(sampleB.size());
    float movA = matrixA.getTotalMovement();
    float movB = matrixB.getTotalMovement();
    static constexpr float MIN_MOV_B = 2.0f;

    bool aValid = (trajLenA >= DTW_MIN_TRAJECTORY_LEN) && (movA >= MIN_GESTURE_MOVEMENT);
    bool bValid = (trajLenB >= DTW_MIN_TRAJECTORY_LEN) && (movB >= MIN_MOV_B);

    if (_trainingIsContext) {
        // Contexto: exigir Sensor A valido. Sensor B e irrelevante.
        if (!aValid) {
            Serial.printf("[GestureEngine] Training sample rejected (CONTEXTO): A(traj=%d mov=%.1f) insuficiente\n",
                          trajLenA, movA);
            return false;
        }
    } else {
        // Objeto: exigir Sensor B valido. Sensor A e irrelevante.
        if (!bValid) {
            Serial.printf("[GestureEngine] Training sample rejected (OBJETO): B(traj=%d mov=%.1f) insuficiente\n",
                          trajLenB, movB);
            return false;
        }
    }

    _trainingSamplesA.push_back(sampleA);
    _trainingSamplesB.push_back(sampleB);

    // FIX INSTALL-20: Salvar trajetorias de gyro
    const std::deque<Point3D>& gyroADeque = matrixAGyro.getTrajectory();
    const std::deque<Point3D>& gyroBDeque = matrixBGyro.getTrajectory();
    std::vector<Point3D> sampleAGyro(gyroADeque.begin(), gyroADeque.end());
    std::vector<Point3D> sampleBGyro(gyroBDeque.begin(), gyroBDeque.end());
    _trainingSamplesAGyro.push_back(sampleAGyro);
    _trainingSamplesBGyro.push_back(sampleBGyro);

    // === Modelo Orbital: extrair e armazenar assinaturas de treino ===
    int sigIdx = static_cast<int>(_trainingSamplesA.size()) - 1;
    if (sigIdx >= 0 && sigIdx < TRAINING_SAMPLES_NEEDED) {
        _trainingSignaturesA[sigIdx] = _capturedSigA;
        _trainingSignaturesB[sigIdx] = _capturedSigB;
        _trainingSignaturesAGyro[sigIdx] = _capturedSigAGyro;

        if (_capturedSigA.valid) {
            DBG2("[Orbital] Training sig %d: amp=%.2f lin=%.2f plane=[%.2f,%.2f,%.2f]\n",
                sigIdx, _capturedSigA.amplitude, _capturedSigA.linearity,
                _capturedSigA.planeNormal[0], _capturedSigA.planeNormal[1],
                _capturedSigA.planeNormal[2]);
        }
    }

    Serial.printf("[GestureEngine] Training sample %d/%d added (A: %d pts, B: %d pts)\n",
                  static_cast<int>(_trainingSamplesA.size()), TRAINING_SAMPLES_NEEDED,
                  static_cast<int>(sampleA.size()), static_cast<int>(sampleB.size()));
    return true;
}

bool GestureEngine::hasEnoughSamples() const {
    return static_cast<int>(_trainingSamplesA.size()) >= TRAINING_SAMPLES_NEEDED;
}

GestureDefinition GestureEngine::computeAverageGesture() const {
    GestureDefinition result;

    if (!hasEnoughSamples()) {
        Serial.println("[GestureEngine] Not enough training samples to compute average");
        return result;
    }

    // --- Pick the median-length sample as reference trajectory ---
    // FIX TRAIN-SENSOR: Ordenar pelo sensor PRINCIPAL do tipo de gesto.
    // Contextos: sensor principal = A. Objetos: sensor principal = B.
    int indices[TRAINING_SAMPLES_NEEDED];
    for (int i = 0; i < TRAINING_SAMPLES_NEEDED; i++) {
        indices[i] = i;
    }

    // Simple sort by size of PRIMARY sensor trajectory (3 elements, bubble sort is fine)
    const auto& primarySamples = _trainingIsContext ? _trainingSamplesA : _trainingSamplesB;
    for (int i = 0; i < TRAINING_SAMPLES_NEEDED - 1; i++) {
        for (int j = i + 1; j < TRAINING_SAMPLES_NEEDED; j++) {
            if (primarySamples[indices[i]].size() > primarySamples[indices[j]].size()) {
                int tmp = indices[i];
                indices[i] = indices[j];
                indices[j] = tmp;
            }
        }
    }

    // Median is the middle element after sorting by length
    int medianIdx = indices[TRAINING_SAMPLES_NEEDED / 2];

    // Use median sample as the reference trajectory
    result.trajectoryA = _trainingSamplesA[medianIdx];
    result.trajectoryB = _trainingSamplesB[medianIdx];
    // FIX INSTALL-20: Gyro trajectories
    if (medianIdx < static_cast<int>(_trainingSamplesAGyro.size())) {
        result.trajectoryAGyro = _trainingSamplesAGyro[medianIdx];
    }
    if (medianIdx < static_cast<int>(_trainingSamplesBGyro.size())) {
        result.trajectoryBGyro = _trainingSamplesBGyro[medianIdx];
    }

    // --- Compute threshold from DTW distances between samples ---
    // Calculate pairwise DTW distances between all samples
    DTW dtwCalc;
    float totalDistance = 0.0f;
    int pairCount = 0;

    // FIX TRAIN-SENSOR: Calcular threshold usando SOMENTE o sensor principal.
    // Contextos = Sensor A. Objetos = Sensor B.
    // O sensor secundario esta zerado (filtro no handleIMU), misturar
    // distancia zero com distancia real distorce o threshold.
    for (int i = 0; i < TRAINING_SAMPLES_NEEDED; i++) {
        for (int j = i + 1; j < TRAINING_SAMPLES_NEEDED; j++) {
            float dist;
            if (_trainingIsContext) {
                // Contexto: threshold baseado em Sensor A
                dist = dtwCalc.calculate(_trainingSamplesA[i], _trainingSamplesA[j]);
            } else {
                // Objeto: threshold baseado em Sensor B
                dist = dtwCalc.calculate(_trainingSamplesB[i], _trainingSamplesB[j]);
            }
            totalDistance += dist;
            pairCount++;
        }
    }

    // Average distance between samples x 1.5 = suggested threshold
    float avgDistance = (pairCount > 0) ? (totalDistance / pairCount) : DTW_THRESHOLD_DEFAULT;
    float suggestedThreshold = avgDistance * 1.5f;

    // FIX INSTALL-17: Threshold minimo de 3.0 para evitar false positives.
    // Com threshold 1.5, qualquer movimento similar ao treino faz match.
    // 3.0 exige similaridade real entre o gesto feito e o treinado.
    if (suggestedThreshold < 3.0f) {
        suggestedThreshold = 3.0f;
    }
    if (suggestedThreshold > DTW_THRESHOLD_DEFAULT * 2.0f) {
        suggestedThreshold = DTW_THRESHOLD_DEFAULT * 2.0f;  // Cap at 2x default
    }

    result.threshold = suggestedThreshold;

    // === Modelo Orbital: calcular assinatura media e threshold orbital ===
    result.signatureA = OrbitalExtractor::average(_trainingSignaturesA, TRAINING_SAMPLES_NEEDED);
    result.signatureB = OrbitalExtractor::average(_trainingSignaturesB, TRAINING_SAMPLES_NEEDED);
    result.signatureAGyro = OrbitalExtractor::average(_trainingSignaturesAGyro, TRAINING_SAMPLES_NEEDED);

    // Log da assinatura orbital computada
    if (result.signatureA.valid) {
        float orbitalMaxDist = OrbitalExtractor::maxPairDistance(
            _trainingSignaturesA, TRAINING_SAMPLES_NEEDED);
        DBG2("[Orbital] Avg signature: amp=%.2f peak=%.2f lin=%.2f dur=%.0f\n",
            result.signatureA.amplitude, result.signatureA.peak,
            result.signatureA.linearity, result.signatureA.duration);
        DBG2("[Orbital] Avg plane: [%.2f, %.2f, %.2f]\n",
            result.signatureA.planeNormal[0], result.signatureA.planeNormal[1],
            result.signatureA.planeNormal[2]);
        DBG2("[Orbital] Max pair distance: %.3f (threshold: %.3f)\n",
            orbitalMaxDist, orbitalMaxDist * 1.5f);
    }

    // FIX TRAIN-SENSOR: Estimar duracao pelo sensor PRINCIPAL (o que tem dados reais).
    // (approximation: points * IMU_SAMPLE_PERIOD_MS)
    size_t primaryTrajLen = _trainingIsContext ? result.trajectoryA.size() : result.trajectoryB.size();
    result.durationMs = static_cast<uint16_t>(primaryTrajLen * IMU_SAMPLE_PERIOD_MS);

    Serial.printf("[GestureEngine] Training computed: medianIdx=%d, threshold=%.2f, duration=%dms, "
                  "trajA=%d pts, trajB=%d pts\n",
                  medianIdx, result.threshold, result.durationMs,
                  static_cast<int>(result.trajectoryA.size()),
                  static_cast<int>(result.trajectoryB.size()));

    // Dump trajetoria completa para analise/debug
    Serial.printf("[TRAJ_A] Sensor A accel (%d pts):\n", (int)result.trajectoryA.size());
    for (size_t i = 0; i < result.trajectoryA.size(); i++) {
        Serial.printf("  [%d] x=%d y=%d z=%d\n",
                      (int)i, result.trajectoryA[i].x,
                      result.trajectoryA[i].y, result.trajectoryA[i].z);
    }
    Serial.printf("[TRAJ_A_GYRO] Sensor A gyro (%d pts):\n", (int)result.trajectoryAGyro.size());
    for (size_t i = 0; i < result.trajectoryAGyro.size(); i++) {
        Serial.printf("  [%d] x=%d y=%d z=%d\n",
                      (int)i, result.trajectoryAGyro[i].x,
                      result.trajectoryAGyro[i].y, result.trajectoryAGyro[i].z);
    }

    return result;
}

void GestureEngine::clearTrainingSamples() {
    _trainingSamplesA.clear();
    _trainingSamplesB.clear();
    _trainingSamplesAGyro.clear();
    _trainingSamplesBGyro.clear();
    // Limpar assinaturas orbitais de treino para evitar dados velhos
    for (int i = 0; i < TRAINING_SAMPLES_NEEDED; i++) {
        _trainingSignaturesA[i] = OrbitalSignature();
        _trainingSignaturesB[i] = OrbitalSignature();
        _trainingSignaturesAGyro[i] = OrbitalSignature();
    }
    Serial.println("[GestureEngine] Training samples cleared");
}

int GestureEngine::getTrainingSampleCount() const {
    return static_cast<int>(_trainingSamplesA.size());
}

HandDominance GestureEngine::getTrainingHandDominance() const {
    // FIX L03: Removido const_cast desnecessario — detectDominantHand() ja e const
    return detectDominantHand();
}
