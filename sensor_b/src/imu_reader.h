/**
 * GESTUUM — IMU Reader (Sensor B)
 * Bloco: 2.1 — Sensor B firmware
 * Responsibility: Read MPU6886 data with EMA filtering.
 */

#ifndef IMU_READER_H
#define IMU_READER_H

#include <M5StickCPlus2.h>
#include "constants.h"

class IMUReader {
public:
    // Initialize IMU (must be called after StickCP2.begin())
    void begin();

    // Read raw IMU data and apply EMA filter
    void update();

    // Filtered accelerometer values (g)
    float getAccelX() const { return filteredAx; }
    float getAccelY() const { return filteredAy; }
    float getAccelZ() const { return filteredAz; }

    // Filtered gyroscope values (deg/s)
    float getGyroX() const { return filteredGx; }
    float getGyroY() const { return filteredGy; }
    float getGyroZ() const { return filteredGz; }

    // Valores escalados para transmissao via ESP-NOW (accel * 1000, gyro * 100)
    // FIX H06: Clamp antes do cast para evitar overflow int16_t.
    // MPU6886 a +/-500 deg/s: 500*100=50000 > int16_t max (32767).
    // Sem clamp, o valor wrapa para negativo → dados corrompidos.
    int16_t getScaledAccelX() const { return clampToInt16(filteredAx * 1000.0f); }
    int16_t getScaledAccelY() const { return clampToInt16(filteredAy * 1000.0f); }
    int16_t getScaledAccelZ() const { return clampToInt16(filteredAz * 1000.0f); }
    int16_t getScaledGyroX() const { return clampToInt16(filteredGx * 100.0f); }
    int16_t getScaledGyroY() const { return clampToInt16(filteredGy * 100.0f); }
    int16_t getScaledGyroZ() const { return clampToInt16(filteredGz * 100.0f); }

private:
    float rawAx = 0.0f, rawAy = 0.0f, rawAz = 0.0f;
    float rawGx = 0.0f, rawGy = 0.0f, rawGz = 0.0f;
    float filteredAx = 0.0f, filteredAy = 0.0f, filteredAz = 0.0f;
    float filteredGx = 0.0f, filteredGy = 0.0f, filteredGz = 0.0f;

    void applyEMAFilter();

    // FIX H06: Clamp float para int16_t sem overflow
    static int16_t clampToInt16(float v) {
        if (v > 32767.0f) return 32767;
        if (v < -32768.0f) return -32768;
        return static_cast<int16_t>(v);
    }
};

#endif // IMU_READER_H
