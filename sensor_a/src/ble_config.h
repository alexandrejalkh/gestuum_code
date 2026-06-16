/**
 * GESTUUM — BLE Configuration Service
 * Responsibility: Provide BLE GATT server for app-based configuration.
 * Activated by holding Button A+B for 3 seconds.
 *
 * Service UUID: 4e5f6a7b-8c9d-0e1f-2a3b-4c5d6e7f8a9b
 * Characteristics:
 *   CMD_TX  (Write) — App sends JSON commands to sensor
 *   CMD_RX  (Notify) — Sensor sends JSON responses to app
 *   AUDIO_TX (Write) — App uploads audio data chunks
 */

#ifndef BLE_CONFIG_H
#define BLE_CONFIG_H

#include <Arduino.h>
#include "constants.h"

// BLE UUIDs
#define GESTUUM_SERVICE_UUID        "4e5f6a7b-8c9d-0e1f-2a3b-4c5d6e7f8a9b"
#define GESTUUM_CMD_TX_UUID         "4e5f6a7b-8c9d-0e1f-2a3b-4c5d6e7f8a9c"
#define GESTUUM_CMD_RX_UUID         "4e5f6a7b-8c9d-0e1f-2a3b-4c5d6e7f8a9d"
#define GESTUUM_AUDIO_TX_UUID       "4e5f6a7b-8c9d-0e1f-2a3b-4c5d6e7f8a9e"

class BLEConfig {
public:
    void begin(const char* deviceName);
    void stop();
    bool isActive() const;
    bool isClientConnected() const;
    void update();  // Call in loop

    // Set callback for when a command is received
    typedef void (*CommandCallback)(const char* json);
    void setCommandCallback(CommandCallback cb);

    // Send response back to app
    void sendResponse(const char* json);

    // Audio upload handling
    bool isUploadInProgress() const;

    // FIX P3: Buffer de comando diferido.
    // Callback BLE roda na task BLE — nao e seguro chamar handleCommand
    // diretamente (acessa SPIFFS, audio, display — recursos do loop).
    // O callback salva o comando aqui, e update() (no loop) despacha.
    // FIX INSTALL-03: Movido para public — callbacks CmdTxCallbacks e
    // AudioTxCallbacks precisam acessar esses membros diretamente.
    // FIX P-Bug1 (2026-05-02 pentest Frank): aumentado de 512 → 2048.
    // Razao: upload_chunk JSON tem ~720 bytes (chunk binario 512B
    // base64-encoded vira ~683B + wrapper JSON ~37B). Com buffer 512,
    // o callback truncava silenciosamente (spaceLeft=0), o '\n'
    // terminator nunca chegava, _cmdPending nunca virava true,
    // e upload via BLE falhava permanentemente. 2048 cobre upload_chunk
    // com folga e ainda deixa margem pra crescimento futuro.
    // Custo: +1.5 KB de RAM (aceitavel — estamos em 23.4%).
    static const int CMD_BUFFER_SIZE = 2048;
    volatile bool _cmdPending = false;
    char _cmdBuffer[CMD_BUFFER_SIZE];

private:
    bool _active = false;
    bool _clientConnected = false;
    bool _uploadInProgress = false;
    CommandCallback _cmdCallback = nullptr;
};

extern BLEConfig bleConfig;

#endif // BLE_CONFIG_H
