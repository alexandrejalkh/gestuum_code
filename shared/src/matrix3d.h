/**
 * GESTUUM — Matrix3D Header
 * Bloco: 1.2 — Biblioteca shared/ - Matrix3D
 * Responsibility: Map raw IMU acceleration data to discrete positions
 *                 in a 7x7x7 3D grid and record movement trajectories.
 */

#ifndef GESTUUM_MATRIX3D_H
#define GESTUUM_MATRIX3D_H

#include "constants.h"
#include <deque>
#include <cmath>

/**
 * Amostra bruta do IMU — armazena aceleracao/gyro sem discretizacao.
 * Usado pelo modelo orbital para extrair features continuas.
 */
struct RawSample3D {
    float x, y, z;
};

/**
 * Represents a discrete point in the 3D grid (0-10 per axis).
 * Default position is the grid center (5,5,5).
 */
struct Point3D {
    int8_t x, y, z;

    Point3D() : x(GRID_CENTER), y(GRID_CENTER), z(GRID_CENTER) {}
    Point3D(int8_t _x, int8_t _y, int8_t _z) : x(_x), y(_y), z(_z) {}

    bool operator==(const Point3D& other) const {
        return x == other.x && y == other.y && z == other.z;
    }

    bool operator!=(const Point3D& other) const {
        return !(*this == other);
    }

    float distanceTo(const Point3D& other) const {
        float dx = x - other.x;
        float dy = y - other.y;
        float dz = z - other.z;
        return sqrt(dx * dx + dy * dy + dz * dz);
    }
};

/**
 * Maps continuous IMU acceleration data to discrete grid cells
 * and tracks movement trajectories for gesture recognition.
 *
 * Pipeline per update():
 *   1. EMA filter on raw acceleration
 *   2. Position integration with damping (return-to-center)
 *   3. Map continuous position to grid cell
 *   4. Record trajectory on cell change (FIFO, max DTW_MAX_TRAJECTORY_LEN)
 *   5. Update lastMovementTime on cell change (for stability detection)
 */
class Matrix3D {
public:
    Matrix3D();

    // Reset all state to center (5,5,5)
    void reset();

    // Process new IMU acceleration sample
    void update(float ax, float ay, float az);

    // Get current discrete grid position
    Point3D getCurrentPosition() const;

    // Get accumulated trajectory (sequence of visited cells)
    const std::deque<Point3D>& getTrajectory() const;

    // Clear trajectory, keep current position as starting point
    void clearTrajectory();

    // True if movement detected and trajectory exceeds minimum length
    bool hasMovement() const;

    /**
     * True if at least one cell change occurred since last reset().
     * Simpler than hasMovement() — no trajectory length requirement.
     * Used to verify that the user actually moved during a recording,
     * preventing false "gesture complete" from residual static data.
     */
    bool hasMoved() const;

    /**
     * Check if position has been stable (near center) for at least stableMs.
     * "Near center" means within 1 cell of [5,5,5] on each axis.
     * Returns true if (millis() - lastMovementTime) > stableMs.
     * @param stableMs Duration in ms the position must remain stable (default 300ms)
     */
    bool isStable(unsigned long stableMs = 300) const;

    /**
     * Get the timestamp (millis) of the last cell position change.
     */
    unsigned long getLastMovementTime() const;

    /**
     * Calculate total movement along the trajectory (sum of euclidean
     * distances between consecutive points). Returns 0 if < 2 points.
     */
    float getTotalMovement() const;

    // === Modelo Orbital — buffer bruto e deteccao de stroke ===

    // Acesso ao buffer de amostras brutas (para extracao de features)
    const RawSample3D* getRawBuffer() const;
    size_t getRawCount() const;

    // Deteccao de stroke — true quando onset E offset foram detectados
    bool isStrokeComplete() const;
    // Indice no rawBuffer onde o stroke comecou
    size_t getStrokeOnsetIdx() const;
    // Indice no rawBuffer onde o stroke terminou
    size_t getStrokeOffsetIdx() const;

private:
    float posX, posY, posZ;                // Continuous position (integrated)
    float filteredX, filteredY, filteredZ;  // EMA-filtered acceleration
    Point3D currentCell;                   // Current grid cell
    Point3D lastCell;                      // Previous grid cell
    std::deque<Point3D> trajectory;        // Sequence of visited cells
    bool moved;                            // Movement detected flag
    unsigned long lastMovementTime;        // Timestamp of last cell change (millis)

    // Map a continuous value to a grid cell index [GRID_MIN, GRID_MAX]
    int8_t mapToGrid(float value);

    // === Modelo Orbital — armazenamento bruto ===
    RawSample3D rawBuffer[ORBITAL_RAW_BUFFER_SIZE]; // Buffer de amostras brutas
    size_t rawCount;                     // Numero de amostras no buffer

    // === Deteccao de stroke ===
    float restMagnitude;                 // Magnitude media em repouso (primeiras amostras)
    bool strokeActive;                   // Stroke em andamento (acima do onset)
    bool strokeComplete;                 // Stroke completo (onset + offset detectados)
    size_t strokeOnsetIdx;               // Indice do inicio do stroke no rawBuffer
    size_t strokeOffsetIdx;              // Indice do fim do stroke no rawBuffer
    uint8_t onsetCounter;                // Amostras consecutivas acima do onset threshold
    uint8_t offsetCounter;               // Amostras consecutivas abaixo do offset threshold
    bool restCalibrated;                 // true apos calibrar magnitude de repouso
    static constexpr size_t REST_CALIBRATION_SAMPLES = 5;  // Amostras para calibrar repouso
    // Cooldown pos-double-tap: ignora as primeiras N amostras antes de calibrar.
    // Double-tap gera vibracao residual de ~200-300ms. A 50Hz, 15 amostras = 300ms.
    static constexpr size_t STROKE_COOLDOWN_SAMPLES = 15;
};

#endif // GESTUUM_MATRIX3D_H
