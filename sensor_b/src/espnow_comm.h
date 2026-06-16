/**
 * GESTUUM — ESP-NOW Communication (Sensor B)
 * Bloco: 2.1 — Sensor B firmware
 * Responsibility: ESP-NOW init, callbacks, and IMU data transmission.
 */

#ifndef ESPNOW_COMM_H
#define ESPNOW_COMM_H

#include <Arduino.h>
#include <stdint.h>
#include "../../shared/src/constants.h"
#include <esp_now.h>
#include "protocol.h"

// Initialize WiFi STA mode, ESP-NOW, register callbacks, add broadcast peer.
// Returns true on success, false on failure.
bool setupESPNow();

// Send an IMUPacket to Sensor A via ESP-NOW.
void sendIMUData(const IMUPacket& packet);

// ESP-NOW send callback (free function, registered internally)
void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);

// ESP-NOW receive callback (free function, registered internally)
void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);

// Send heartbeat back to Sensor A (bidirectional connection monitoring)
void sendHeartbeat();

// FIX C07: Processa peer add diferido do callback.
// DEVE ser chamado no loop() principal.
void processESPNowPairing();

// FIX BUG-08: Processa troca de canal diferida do callback.
// DEVE ser chamado no loop() principal.
void processChannelChange();

// FIX SERIAL-03: Canal cacheado (lido no init, sem NVS no loop)
extern uint8_t cachedChannel;

// FIX INSTALL-09: Spinlock exportado — usado por main.cpp para proteger sensorA_mac
extern portMUX_TYPE macMux;

#endif // ESPNOW_COMM_H
