/**
 * GESTUUM — Sensor A Configuration & Global State
 * Bloco: 3.6 — Main State Machine
 * Responsibility: Define system states, global variables, and timing for the
 *                 Sensor A main state machine.
 */

#ifndef GESTUUM_CONFIG_H
#define GESTUUM_CONFIG_H

#include <stdint.h>
#include "constants.h"

// Sprint C3c (Caminho C, 2026-05-02): centralizado aqui pra menu local "Sobre"
// e config_handler.cpp consumirem do mesmo lugar (era duplicado).
#define GESTUUM_FW_VERSION "1.0.0"

// FIX SERIAL-04: Nivel de debug controlavel.
// 0 = minimo (erros + estados), 1 = normal, 2 = verbose (TAP, Orbital, etc.)
// A 115200 baud sem flow control, prints excessivos enchem o TX buffer
// e bloqueiam o loop inteiro (Serial.printf espera espaco no buffer).
#define DEBUG_LEVEL 0

// Macros de debug — prints que so existem em nivel >= N
#if DEBUG_LEVEL >= 2
  #define DBG2(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG2(fmt, ...) ((void)0)
#endif
#if DEBUG_LEVEL >= 1
  #define DBG1(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#else
  #define DBG1(fmt, ...) ((void)0)
#endif

// === System States ===
enum SystemState : uint8_t {
    STATE_INIT,         // Initializing all components
    STATE_PAIRING,      // Waiting for Sensor B connection via ESP-NOW
    STATE_IDLE,         // Ready to receive gesture (push-to-talk)
    STATE_RECORDING,    // Capturing gesture IMU data
    STATE_MATCHING,     // Processing DTW recognition
    STATE_SPEAKING,     // Playing audio feedback
    STATE_EMERGENCY,    // Emergency mode active
    STATE_CONTEXT_WAIT, // Waiting for object gesture after context detected
    STATE_TRAINING,     // Training mode: recording gesture samples for calibration
    STATE_MENU          // Caminho C (2026-05-02): menu local navegavel pelos botoes
};

// === Global State Variables ===

// Current state of the system state machine.
extern SystemState currentState;

// True when system is active (paired and operational).
extern bool systemActive;

// Timestamp when gesture recording started (for timeout detection).
extern unsigned long recordStartTime;

// Timestamp of last heartbeat sent to Sensor B.
extern unsigned long lastHeartbeatSent;

// Timestamp of last IMU sample read (for 50Hz sampling).
extern unsigned long lastIMURead;

// === Gesture Level State ===

// Currently active gesture capture level.
extern GestureLevel currentLevel;

// === Voice & Profile State ===

// Currently selected voice for audio output.
extern Voice currentVoice;

// Active profiles loaded alongside base categories.
extern Profile activeProfiles[MAX_ACTIVE_PROFILES];
extern uint8_t activeProfileCount;

// === Timeouts ===

// FIX ALT-12: Defines mortos removidos (DOUBLE_TAP_ACCEL_THRESHOLD, MIN_RECORDING_DURATION_MS).
// Valores reais vem de LEVEL_CONFIGS em constants.cpp (doubleTapThreshold, minRecordingMs).

// FIX FLOW-03: Timeout para esperar gesto de objeto apos contexto.
// 3000ms era muito curto — usuario precisa ouvir "I want" (~1s) + double-tap
// + fazer gesto (~2s). 8 segundos da tempo confortavel.
#define CONTEXT_TIMEOUT_MS 8000

#endif // GESTUUM_CONFIG_H
