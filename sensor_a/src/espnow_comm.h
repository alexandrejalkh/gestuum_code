/**
 * GESTUUM — ESP-NOW Communication (Sensor A)
 * Bloco: 3.1 — Sensor A - ESP-NOW communication + pairing
 * Responsibility: ESP-NOW init, callbacks, peer management, send functions.
 *
 * Fix C1: Double-buffering for IMU data. ISR writes to one buffer,
 *         main loop reads from the other via espnow_getIMUData().
 */

#ifndef SENSOR_A_ESPNOW_COMM_H
#define SENSOR_A_ESPNOW_COMM_H

#include <stdint.h>
#include "constants.h"
#include <esp_now.h>
#include "protocol.h"

// --- Public state (accessed by main loop and other modules) ---

// Set to true inside OnDataRecv when a new IMUPacket arrives.
// Main loop must reset to false after consuming the data.
extern volatile bool newIMUDataAvailable;

// MAC address of Sensor B (discovered during pairing).
extern uint8_t sensorB_mac[6];

// Broadcast address FF:FF:FF:FF:FF:FF.
extern uint8_t broadcast_mac[6];

// True once Sensor B has been discovered and added as a peer.
extern volatile bool sensorBPaired;

// --- Initialization ---

// Configure WiFi STA, init ESP-NOW, register callbacks, add broadcast peer.
void espnow_init();

// --- Send functions ---

// Send a heartbeat broadcast (sender=0 for Sensor A).
void espnow_send_heartbeat();

// Send a category change broadcast.
void espnow_send_category(Category cat);

// Send a recognized gesture broadcast.
void espnow_send_gesture(uint8_t category, uint16_t gestureId,
                          uint8_t automationCmd, bool active);

// Send an emergency alert broadcast.
void espnow_send_emergency(uint8_t level, uint16_t gestureId);

// Send an automation command broadcast.
void espnow_send_automation(uint8_t command, uint8_t param);

// Broadcast troca de canal para todos os dispositivos.
// Salva no NVS, broadcast MSG_SET_CHANNEL, e reinicia apos 1s.
void espnow_set_channel(uint8_t newChannel);

// FIX P-Bug3 (2026-05-02 pentest Frank): retorna canal ativo runtime.
// Necessario pra menu local "Sobre" exibir canal real (apos set_channel
// via BLE, o ESPNOW_CHANNEL define ainda diz 13 mas operacao roda em outro).
uint8_t espnow_get_channel();

// --- Query functions ---

// Returns true if Sensor B is paired AND its last heartbeat was within
// CONNECTION_TIMEOUT_MS.
bool espnow_is_sensor_b_connected();

// Returns the timestamp field from the last received IMUPacket.
unsigned long espnow_last_imu_timestamp();

// Safely copy the latest complete IMU packet into the caller's buffer.
// Uses double-buffering + spinlock to avoid ISR/main-loop data races.
// Returns true if a new packet was available, false otherwise.
bool espnow_getIMUData(IMUPacket* out);

// FIX C05: Processa pairing diferido do Sensor B.
// DEVE ser chamado no loop() principal — o callback ESP-NOW so seta
// uma flag, e esta funcao faz o pairing real (Serial + esp_now_add_peer).
void espnow_processPairing();

// FIX BUG-11: Flag indicando que Sensor B enviou SOS.
// Consultada no loop() do main.cpp para ativar emergencia.
extern volatile bool emergencyFromSensorB;

// --- Callbacks (registrados internamente, declarados para visibilidade) ---

void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status);
void onDataRecv(const uint8_t* mac, const uint8_t* data, int len);

#endif // SENSOR_A_ESPNOW_COMM_H
