/**
 * ============================================================
 * GESTUUM — IMU Reader (Sensor B)
 * ============================================================
 * Identico ao Sensor A — le MPU6886, aplica filtro EMA.
 * Dados filtrados sao escalados e transmitidos via ESP-NOW
 * para o Sensor A a 50Hz.
 *
 * FIX H05: Filtro EMA inicializado com leitura real.
 * ============================================================
 */

#include "imu_reader.h"

/**
 * FIX H05: Inicializa filtro EMA com leitura real do sensor.
 * Ver sensor_a/imu_reader.cpp para explicacao detalhada.
 */
// NOTA ALT-13: Sensor A usa M5.Imu (M5Unified) para evitar SIOF.
// Sensor B usa StickCP2.Imu que funciona porque begin() e chamado
// apos StickCP2.begin(). Se der crash no boot, trocar para M5Unified.
void IMUReader::begin() {
    StickCP2.Imu.getAccelData(&rawAx, &rawAy, &rawAz);
    StickCP2.Imu.getGyroData(&rawGx, &rawGy, &rawGz);

    filteredAx = rawAx; filteredAy = rawAy; filteredAz = rawAz;
    filteredGx = rawGx; filteredGy = rawGy; filteredGz = rawGz;
}

void IMUReader::update() {
    // FIX ALT-01: Imu.update() PRECISA ser chamado antes de ler dados.
    // Sem isso, getAccelData() retorna valores estaticos (gravidade apenas).
    // Sensor A ja tinha esse fix (INSTALL-15), Sensor B nao.
    StickCP2.Imu.update();

    // Read raw accelerometer data (units: g)
    StickCP2.Imu.getAccelData(&rawAx, &rawAy, &rawAz);

    // Read raw gyroscope data (units: deg/s)
    StickCP2.Imu.getGyroData(&rawGx, &rawGy, &rawGz);

    // Apply exponential moving average filter
    applyEMAFilter();
}

void IMUReader::applyEMAFilter() {
    filteredAx = EMA_ALPHA * rawAx + (1.0f - EMA_ALPHA) * filteredAx;
    filteredAy = EMA_ALPHA * rawAy + (1.0f - EMA_ALPHA) * filteredAy;
    filteredAz = EMA_ALPHA * rawAz + (1.0f - EMA_ALPHA) * filteredAz;

    filteredGx = EMA_ALPHA * rawGx + (1.0f - EMA_ALPHA) * filteredGx;
    filteredGy = EMA_ALPHA * rawGy + (1.0f - EMA_ALPHA) * filteredGy;
    filteredGz = EMA_ALPHA * rawGz + (1.0f - EMA_ALPHA) * filteredGz;
}
