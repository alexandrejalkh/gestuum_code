/**
 * ============================================================
 * GESTUUM — Sensor B - Estado Global
 * ============================================================
 * Variaveis compartilhadas entre o callback ESP-NOW (task WiFi)
 * e o loop() principal (task Arduino).
 *
 * FIX C08: Todas as variaveis escritas no callback ESP-NOW
 * DEVEM ser volatile — sem isso, o compilador pode cachear
 * o valor em registrador e o loop() nunca ve a atualizacao.
 *
 * sensorA_mac: protegido por portMUX porque sao 6 bytes
 * (nao atomico em arquitetura 32-bit).
 * ============================================================
 */

#ifndef SENSOR_B_CONFIG_H
#define SENSOR_B_CONFIG_H

#include <stdint.h>

// MAC do Sensor A (descoberto via heartbeat)
// Protegido por spinlock no espnow_comm.cpp (6 bytes = nao atomico)
extern uint8_t sensorA_mac[6];

// FIX C08: volatile — escritos no callback WiFi, lidos no loop()
extern volatile bool isConnected;
extern volatile unsigned long lastHeartbeat;
extern volatile uint8_t currentCategory;
extern volatile bool displayDirty;

#endif // SENSOR_B_CONFIG_H
