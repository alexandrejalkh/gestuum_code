/**
 * GESTUUM — USB Serial Configuration Handler Implementation
 * Responsibility: Read JSON commands line-by-line from USB Serial and dispatch them.
 *
 * Protocol:
 *   - Each command is a single line of JSON terminated by '\n'
 *   - Maximum line length: 1024 bytes (buffer overflow resets the buffer)
 *   - Responses are sent as single-line JSON via Serial.println()
 *
 * This handler is always active regardless of BLE config mode state,
 * allowing USB-based configuration and debugging at all times.
 */

#include "serial_config.h"

// === Global instance ===
GestuumSerialConfig serialConfig;

static const char* TAG = "GestuumSerialConfig";

// ============================================================================
// begin() — Initialize serial config handler
// ============================================================================

void GestuumSerialConfig::begin() {
    _cmdCallback = nullptr;
    _bufferPos = 0;
    memset(_buffer, 0, sizeof(_buffer));

    Serial.printf("[%s] Serial config handler initialized\n", TAG);
}

// ============================================================================
// update() — Read characters from Serial and process complete lines
// ============================================================================

void GestuumSerialConfig::update() {
    while (Serial.available()) {
        char c = (char)Serial.read();

        // Newline terminates a command
        if (c == '\n' || c == '\r') {
            if (_bufferPos > 0) {
                // Null-terminate the buffer
                _buffer[_bufferPos] = '\0';

                // Skip empty lines and non-JSON content
                // Simple check: valid JSON commands start with '{'
                if (_buffer[0] == '{') {
                    Serial.printf("[%s] Command received (%d bytes)\n",
                                  TAG, _bufferPos);

                    if (_cmdCallback != nullptr) {
                        _cmdCallback(_buffer);
                    }
                }

                // Reset buffer for next command
                _bufferPos = 0;
                memset(_buffer, 0, sizeof(_buffer));
            }
            continue;
        }

        // Append character to buffer
        if (_bufferPos < (int)(sizeof(_buffer) - 1)) {
            _buffer[_bufferPos] = c;
            _bufferPos++;
        } else {
            // Buffer overflow — discard current line and reset
            Serial.printf("[%s] WARNING: Buffer overflow, discarding line\n", TAG);
            _bufferPos = 0;
            memset(_buffer, 0, sizeof(_buffer));
        }
    }
}

// ============================================================================
// setCommandCallback()
// ============================================================================

void GestuumSerialConfig::setCommandCallback(CommandCallback cb) {
    _cmdCallback = cb;
}

// ============================================================================
// sendResponse() — Send JSON response via Serial
// ============================================================================

void GestuumSerialConfig::sendResponse(const char* json) {
    Serial.println(json);
}
