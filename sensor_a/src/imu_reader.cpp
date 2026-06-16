/**
 * ============================================================
 * GESTUUM — IMU Reader (Sensor A)
 * ============================================================
 * Le acelerometro e giroscopio do MPU6886 integrado no
 * M5StickC Plus2 e aplica filtro EMA (Exponential Moving Average)
 * para suavizar ruido do sensor.
 *
 * EMA: filtered = alpha * raw + (1-alpha) * filtered_anterior
 * Com alpha = 0.3, o filtro suaviza ruido mantendo resposta
 * rapida o suficiente para capturar gestos humanos.
 * ============================================================
 */

#include "imu_reader.h"
#include <M5Unified.h>  // FIX INSTALL-11: M5.Imu em vez de StickCP2.Imu (SIOF)

/**
 * FIX H05: Inicializa filtro EMA com leitura real do sensor.
 *
 * Antes: filtros iniciavam em 0.0 para todos os eixos.
 * Problema: acelerometro em repouso le ~1.0g no eixo Z (gravidade).
 * Com alpha=0.3, o filtro leva ~150ms para convergir de 0 para 1.0.
 * Durante esse tempo, dados sao lixo — pode triggerar double-tap
 * falso ou gesto fantasma.
 *
 * Agora: le o sensor uma vez e usa como valor inicial do filtro.
 */
void IMUReader::begin() {
    // Le IMU uma vez para semear o filtro EMA com valores reais
    M5.Imu.getAccelData(&rawAx, &rawAy, &rawAz);
    M5.Imu.getGyroData(&rawGx, &rawGy, &rawGz);

    // Inicializa filtros com leitura real (nao zero)
    filteredAx = rawAx; filteredAy = rawAy; filteredAz = rawAz;
    filteredGx = rawGx; filteredGy = rawGy; filteredGz = rawGz;
}

void IMUReader::update() {
    // FIX INSTALL-15: M5.Imu.update() PRECISA ser chamado antes de ler dados.
    // M5.update() NAO chama Imu.update() internamente.
    // Sem isso, getAccelData() retorna valores estaticos (gravidade apenas).
    // Grounding: exemplo oficial M5StickCPlus2/examples/Basic/imu/imu.ino
    // usa StickCP2.Imu.update() antes de getImuData().
    M5.Imu.update();

    // Read raw accelerometer data (units: g)
    M5.Imu.getAccelData(&rawAx, &rawAy, &rawAz);

    // Read raw gyroscope data (units: deg/s)
    M5.Imu.getGyroData(&rawGx, &rawGy, &rawGz);

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
