/**
 * GESTUUM — ESP-NOW Protocol Definitions
 * Bloco: 1.1 — Setup PlatformIO + Biblioteca shared/
 * Responsibility: Define all message types and packed structs for ESP-NOW communication.
 */

#ifndef GESTUUM_PROTOCOL_H
#define GESTUUM_PROTOCOL_H

#include <stdint.h>
#include <cstring>

// === PROTOCOL VERSION ===
#define PROTOCOL_VERSION 1

// === MESSAGE TYPES ===
enum MessageType : uint8_t {
    MSG_IMU_DATA    = 0x01,  // Sensor B -> A: IMU data
    MSG_CATEGORY    = 0x02,  // A -> broadcast: category change
    MSG_GESTURE     = 0x03,  // A -> AtomS3: recognized gesture
    MSG_EMERGENCY   = 0x04,  // A -> broadcast: emergency alert
    MSG_HEARTBEAT   = 0x05,  // A <-> B: keep-alive
    MSG_AUTOMATION  = 0x06,  // A -> AtomS3: automation command
    MSG_SET_CHANNEL = 0x07   // A -> broadcast: todos mudam de canal ESP-NOW
};

// === AUTOMATION COMMANDS ===
enum AutomationCmd : uint8_t {
    CMD_NONE        = 0x00,
    CMD_LIGHT_ON    = 0x01,
    CMD_LIGHT_OFF   = 0x02,
    CMD_COLOR_RED   = 0x03,
    CMD_COLOR_GREEN = 0x04,
    CMD_COLOR_BLUE  = 0x05,
    CMD_COLOR_YELLOW = 0x06,
    CMD_COLOR_PURPLE = 0x07,
    CMD_COLOR_WHITE = 0x08,
    CMD_SOS         = 0x09,
    CMD_BRIGHT_UP   = 0x0A,
    CMD_BRIGHT_DOWN = 0x0B
};

// === ESP-NOW PACKETS ===

// IMU data from secondary sensor (Sensor B -> Sensor A) - 20 bytes
// FIX A↔B #7: Adicionado seq (sequence number) para detectar packet loss.
// Contador uint16_t que wrapa em 65535. Sensor A detecta gaps na sequencia.
struct __attribute__((packed)) IMUPacket {
    uint8_t type;
    uint32_t timestamp;      // millis()
    uint16_t seq;            // Sequence number (wrapping counter)
    int16_t ax, ay, az;      // Acceleration * 1000
    int16_t gx, gy, gz;      // Gyroscope * 100
    uint8_t battery;         // 0-100%

    IMUPacket() : type(MSG_IMU_DATA), timestamp(0), seq(0),
                  ax(0), ay(0), az(0),
                  gx(0), gy(0), gz(0),
                  battery(0) {}
};

// Category change notification (Sensor A -> broadcast) - 2 bytes
struct __attribute__((packed)) CategoryPacket {
    uint8_t type;
    uint8_t category;        // Category enum value

    CategoryPacket() : type(MSG_CATEGORY), category(0) {}
};

// Recognized gesture result (Sensor A -> AtomS3) - 6 bytes
struct __attribute__((packed)) GesturePacket {
    uint8_t type;
    uint8_t category;
    uint16_t gesture_id;
    uint8_t automation_cmd;  // AutomationCmd or 0
    uint8_t active;          // System active flag

    GesturePacket() : type(MSG_GESTURE), category(0),
                      gesture_id(0), automation_cmd(0),
                      active(0) {}
};

// Emergency alert broadcast (Sensor A -> broadcast) - 4 bytes
struct __attribute__((packed)) EmergencyPacket {
    uint8_t type;
    uint8_t level;           // 1=normal, 2=urgent, 3=critical
    uint16_t gesture_id;

    // FIX M05: Default level 1 (normal) ao inves de 0 (indefinido)
    EmergencyPacket() : type(MSG_EMERGENCY), level(1),
                        gesture_id(0) {}
};

// Keep-alive heartbeat (A <-> B) - 3 bytes
struct __attribute__((packed)) HeartbeatPacket {
    uint8_t type;
    uint8_t sender;          // 0=A, 1=B
    uint8_t battery;

    HeartbeatPacket() : type(MSG_HEARTBEAT), sender(0),
                        battery(0) {}
};

// Automation command for LED/device control (A -> AtomS3) - 3 bytes
struct __attribute__((packed)) AutomationPacket {
    uint8_t type;
    uint8_t command;         // AutomationCmd value
    uint8_t param;           // Optional parameter

    AutomationPacket() : type(MSG_AUTOMATION), command(0),
                         param(0) {}
};

// Troca de canal ESP-NOW (A -> broadcast) - 2 bytes
// Todos os dispositivos recebem, salvam no NVS e reiniciam no novo canal.
struct __attribute__((packed)) SetChannelPacket {
    uint8_t type;
    uint8_t channel;         // Novo canal WiFi (1-13)

    SetChannelPacket() : type(MSG_SET_CHANNEL), channel(ESPNOW_CHANNEL) {}
};

// FIX M04: Validacao em compile-time dos tamanhos das structs packed.
// Se algum compilador ignorar __attribute__((packed)) ou adicionar
// padding, o static_assert falha e impede build com dados corrompidos.
static_assert(sizeof(IMUPacket) == 20,       "IMUPacket must be 20 bytes");
static_assert(sizeof(CategoryPacket) == 2,   "CategoryPacket must be 2 bytes");
static_assert(sizeof(GesturePacket) == 6,    "GesturePacket must be 6 bytes");
static_assert(sizeof(EmergencyPacket) == 4,  "EmergencyPacket must be 4 bytes");
static_assert(sizeof(HeartbeatPacket) == 3,  "HeartbeatPacket must be 3 bytes");
static_assert(sizeof(AutomationPacket) == 3, "AutomationPacket must be 3 bytes");
static_assert(sizeof(SetChannelPacket) == 2, "SetChannelPacket must be 2 bytes");

#endif // GESTUUM_PROTOCOL_H
