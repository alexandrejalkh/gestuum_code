/**
 * GESTUUM — Matrix3D Implementation
 * Bloco: 1.2 — Biblioteca shared/ - Matrix3D
 * Responsibility: Full implementation of IMU-to-grid mapping and trajectory tracking.
 */

#include "matrix3d.h"
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#else
// For unit testing outside Arduino environment
extern unsigned long millis();
#endif

Matrix3D::Matrix3D() {
    reset();
}

void Matrix3D::reset() {
    posX = 0.0f;
    posY = 0.0f;
    posZ = 0.0f;
    filteredX = 0.0f;
    filteredY = 0.0f;
    filteredZ = 0.0f;
    currentCell = Point3D(GRID_CENTER, GRID_CENTER, GRID_CENTER);
    lastCell = currentCell;
    trajectory.clear();
    trajectory.push_back(currentCell);
    moved = false;
    lastMovementTime = millis();

    // Reset do buffer bruto e estado do stroke
    rawCount = 0;
    restMagnitude = 1.0f;  // Assume gravidade como default
    strokeActive = false;
    strokeComplete = false;
    strokeOnsetIdx = 0;
    strokeOffsetIdx = 0;
    onsetCounter = 0;
    offsetCounter = 0;
    restCalibrated = false;
}

void Matrix3D::update(float ax, float ay, float az) {
    // === Modelo Orbital: armazenar amostra bruta ===
    if (rawCount < ORBITAL_RAW_BUFFER_SIZE) {
        rawBuffer[rawCount].x = ax;
        rawBuffer[rawCount].y = ay;
        rawBuffer[rawCount].z = az;
        rawCount++;
    }

    // === Deteccao de stroke ===
    float mag = sqrtf(ax * ax + ay * ay + az * az);

    // Cooldown pos-double-tap: ignora primeiras amostras (vibracao residual).
    // Apos o cooldown, calibra repouso com as proximas 5 amostras.
    if (rawCount <= STROKE_COOLDOWN_SAMPLES) {
        // Ainda no cooldown — nao faz nada (nem calibra, nem detecta onset)
    } else if (!restCalibrated) {
        size_t calibIdx = rawCount - STROKE_COOLDOWN_SAMPLES;
        if (calibIdx <= REST_CALIBRATION_SAMPLES) {
            // Media incremental das amostras pos-cooldown
            restMagnitude = restMagnitude + (mag - restMagnitude) / calibIdx;
        }
        if (calibIdx == REST_CALIBRATION_SAMPLES) {
            restCalibrated = true;
        }
    }

    // State machine de onset/offset — so roda apos calibrar repouso
    if (restCalibrated) {
        float deviation = fabsf(mag - restMagnitude);

        if (!strokeActive && !strokeComplete) {
            // Procurando onset — inicio do gesto
            if (deviation > ORBITAL_ONSET_THRESHOLD_G) {
                onsetCounter++;
                if (onsetCounter >= ORBITAL_ONSET_SAMPLES) {
                    strokeActive = true;
                    // Onset comecou ORBITAL_ONSET_SAMPLES atras
                    strokeOnsetIdx = (rawCount > ORBITAL_ONSET_SAMPLES)
                        ? (rawCount - ORBITAL_ONSET_SAMPLES) : 0;
                }
            } else {
                onsetCounter = 0;
            }
        } else if (strokeActive && !strokeComplete) {
            // Procurando offset — fim do gesto
            if (deviation < ORBITAL_OFFSET_THRESHOLD_G) {
                offsetCounter++;
                if (offsetCounter >= ORBITAL_OFFSET_SAMPLES) {
                    strokeComplete = true;
                    strokeActive = false;
                    // Offset terminou ORBITAL_OFFSET_SAMPLES atras
                    strokeOffsetIdx = (rawCount > ORBITAL_OFFSET_SAMPLES)
                        ? (rawCount - ORBITAL_OFFSET_SAMPLES) : rawCount;
                }
            } else {
                offsetCounter = 0;
            }
        }
    } // fim do if (restCalibrated)

    // Step 1: Apply EMA (Exponential Moving Average) filter
    filteredX = EMA_ALPHA * ax + (1.0f - EMA_ALPHA) * filteredX;
    filteredY = EMA_ALPHA * ay + (1.0f - EMA_ALPHA) * filteredY;
    filteredZ = EMA_ALPHA * az + (1.0f - EMA_ALPHA) * filteredZ;

    // Step 2: Integrate position with damping (blends toward filtered value)
    posX = posX * DAMPING_FACTOR + filteredX * (1.0f - DAMPING_FACTOR);
    posY = posY * DAMPING_FACTOR + filteredY * (1.0f - DAMPING_FACTOR);
    posZ = posZ * DAMPING_FACTOR + filteredZ * (1.0f - DAMPING_FACTOR);

    // Step 3: Map continuous positions to discrete grid cells
    lastCell = currentCell;
    currentCell.x = mapToGrid(posX);
    currentCell.y = mapToGrid(posY);
    currentCell.z = mapToGrid(posZ);

    // Step 4: Record trajectory if cell changed, and update lastMovementTime
    if (currentCell != lastCell) {
        trajectory.push_back(currentCell);
        moved = true;
        lastMovementTime = millis();

        // FIFO: remove oldest point if trajectory exceeds max length
        if (trajectory.size() > DTW_MAX_TRAJECTORY_LEN) {
            trajectory.pop_front();
        }
    }
}

Point3D Matrix3D::getCurrentPosition() const {
    return currentCell;
}

const std::deque<Point3D>& Matrix3D::getTrajectory() const {
    return trajectory;
}

void Matrix3D::clearTrajectory() {
    trajectory.clear();
    trajectory.push_back(currentCell);
    moved = false;
}

bool Matrix3D::hasMovement() const {
    return moved && trajectory.size() > DTW_MIN_TRAJECTORY_LEN;
}

bool Matrix3D::hasMoved() const {
    return moved;
}

bool Matrix3D::isStable(unsigned long stableMs) const {
    // FIX INSTALL-16: Estavel = celula nao mudou por stableMs milissegundos.
    // Antes: exigia estar "perto do centro" [4-6, 4-6, 4-6], mas com
    // ACCEL_RANGE=2.0g a gravidade mapeia Z para celula ~8, nunca no centro.
    // Agora: basta que a posicao na grid esteja parada (sem mudanca de celula).
    // Isso detecta corretamente quando o usuario parou de mover a mao,
    // independente da orientacao do sensor (gravidade em qualquer eixo).
    unsigned long now = millis();
    return (now - lastMovementTime) >= stableMs;
}

unsigned long Matrix3D::getLastMovementTime() const {
    return lastMovementTime;
}

float Matrix3D::getTotalMovement() const {
    if (trajectory.size() < 2) {
        return 0.0f;
    }

    float total = 0.0f;
    for (size_t i = 1; i < trajectory.size(); i++) {
        total += trajectory[i - 1].distanceTo(trajectory[i]);
    }
    return total;
}

/**
 * Mapeia valor continuo de posicao para celula discreta no grid [0-10].
 *
 * FIX M02: Cast para int (nao int8_t) antes do clamp.
 * Antes: round(normalized) → int8_t → clamp. Se normalized > 122,
 * o cast para int8_t causa overflow (implementation-defined).
 * Agora: round → int → clamp → int8_t (seguro).
 *
 * FIX L02: abs() agora usa int explicito para evitar ambiguidade
 * de overload em toolchains embedded.
 */
// === Modelo Orbital — metodos de acesso ===

const RawSample3D* Matrix3D::getRawBuffer() const {
    return rawBuffer;
}

size_t Matrix3D::getRawCount() const {
    return rawCount;
}

bool Matrix3D::isStrokeComplete() const {
    return strokeComplete;
}

size_t Matrix3D::getStrokeOnsetIdx() const {
    return strokeOnsetIdx;
}

size_t Matrix3D::getStrokeOffsetIdx() const {
    return strokeOffsetIdx;
}

int8_t Matrix3D::mapToGrid(float value) {
    // Normaliza de +/-ACCEL_RANGE para +/-GRID_CENTER
    float normalized = (value / ACCEL_RANGE) * GRID_CENTER;

    // FIX M02: Cast para int primeiro (range seguro), depois clamp
    int cell = static_cast<int>(round(normalized)) + GRID_CENTER;

    // Clamp para range valido do grid [0, 10]
    if (cell < GRID_MIN) cell = GRID_MIN;
    if (cell > GRID_MAX) cell = GRID_MAX;

    return static_cast<int8_t>(cell);
}
