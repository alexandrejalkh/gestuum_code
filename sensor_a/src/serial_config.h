/**
 * GESTUUM — USB Serial Configuration Handler
 * Responsibility: Process JSON configuration commands received via USB Serial.
 * Always active (does not require BLE config mode).
 */

#ifndef SERIAL_CONFIG_H
#define SERIAL_CONFIG_H

#include <Arduino.h>

class GestuumSerialConfig {
public:
    void begin();
    void update();  // Call in loop — reads Serial, parses JSON

    typedef void (*CommandCallback)(const char* json);
    void setCommandCallback(CommandCallback cb);

    void sendResponse(const char* json);

private:
    CommandCallback _cmdCallback;
    char _buffer[1024];
    int _bufferPos;
};

extern GestuumSerialConfig serialConfig;

#endif // SERIAL_CONFIG_H
