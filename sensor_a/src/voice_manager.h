/**
 * GESTUUM — Voice & Profile Manager
 * Responsibility: Manage voice selection, active profiles, and device name
 *                 with NVS persistence.
 */

#ifndef VOICE_MANAGER_H
#define VOICE_MANAGER_H

#include "constants.h"

class VoiceManager {
public:
    void begin();

    // Voice
    Voice getCurrentVoice() const;
    void setVoice(Voice voice);
    const char* getVoicePath() const;   // Returns "/audio/homem/", "/audio/mulher/", etc.
    const char* getVoiceName() const;   // Returns "Homem", "Mulher", "Menino", "Menina"

    // Profile
    bool isProfileActive(Profile profile) const;
    void activateProfile(Profile profile);
    void deactivateProfile(Profile profile);
    uint8_t getActiveProfileCount() const;

    // Device name (BLE advertising name)
    const char* getDeviceName() const;
    void setDeviceName(const char* name);

    // Gesture capture level (configurable sensitivity for different motor abilities)
    GestureLevel getGestureLevel() const;
    void setGestureLevel(GestureLevel level);
    const LevelConfig& getLevelConfig() const;

    // Fix UX12: Silent error mode (for autistic children)
    bool getSilentErrors() const;
    void setSilentErrors(bool silent);

    // Persistence
    void saveToFlash();
    void loadFromFlash();

private:
    Voice _voice;
    GestureLevel _gestureLevel;
    bool _activeProfiles[PROFILE_COUNT];
    char _deviceName[24];  // "GESTUUM-XXXX" + null terminator
    bool _silentErrors;    // Fix UX12: suppress error beeps

    void generateDefaultName();
};

#endif // VOICE_MANAGER_H
