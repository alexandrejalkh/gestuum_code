/**
 * GESTUUM — Voice & Profile Manager Implementation
 * Responsibility: Manage voice selection, active profiles, and device name
 *                 with NVS persistence.
 *
 * Uses ESP32 Preferences library (NVS) to persist voice, profile settings,
 * and BLE device name across power cycles.
 *
 * PROFILE_AUTOMACAO is always active (like PROFILE_BASE) and cannot be deactivated.
 *
 * Default device name: "GESTUUM-XXXX" where XXXX is the last 4 hex digits
 * of the ESP32 MAC address.
 */

#include "voice_manager.h"
#include <Preferences.h>
#include <Arduino.h>
#include <WiFi.h>
#include <cstring>

static const char* TAG = "VoiceManager";
static const char* NVS_NAMESPACE = "gestuum_vm";

// ============================================================================
// begin()
// ============================================================================

void VoiceManager::begin() {
    // Initialize defaults
    _voice = VOICE_HOMEM;
    _gestureLevel = LEVEL_STANDARD;
    _silentErrors = false;
    // Todos os perfis ativos por padrao — gestos e audios existem para todos
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        _activeProfiles[i] = true;
    }

    // Generate default device name from MAC address
    generateDefaultName();

    // Load saved preferences (overrides defaults if available)
    loadFromFlash();

    Serial.printf("[%s] Initialized — voice: %s, name: %s, active profiles: %d\n",
                  TAG, getVoiceName(), _deviceName, getActiveProfileCount());
}

// ============================================================================
// generateDefaultName() — Create "GESTUUM-XXXX" from MAC address
// ============================================================================

void VoiceManager::generateDefaultName() {
    uint8_t mac[6];
    WiFi.macAddress(mac);

    // Use last 2 bytes of MAC as hex suffix
    snprintf(_deviceName, sizeof(_deviceName), "GESTUUM-%02X%02X", mac[4], mac[5]);
}

// ============================================================================
// Voice methods
// ============================================================================

Voice VoiceManager::getCurrentVoice() const {
    return _voice;
}

void VoiceManager::setVoice(Voice voice) {
    if (voice >= VOICE_COUNT) {
        Serial.printf("[%s] Invalid voice %d, ignoring\n", TAG, static_cast<int>(voice));
        return;
    }
    _voice = voice;
    Serial.printf("[%s] Voice changed to %s\n", TAG, getVoiceName());
}

/**
 * Retorna o path SPIFFS da pasta de audio da voz ativa.
 *
 * FIX CRITICAL: Paths encurtados de /audio/homem/ (15 chars) para /a/h/ (5 chars).
 * SPIFFS no ESP32 tem limite de 31 chars por path completo.
 * Com /audio/menina/ctx_e07_nao_preciso_de.wav = 40 chars → FALHA.
 * Com /a/i/ctx_e07_nao_preciso_de.wav = 30 chars → OK.
 *
 * Mapeamento: homem→h, mulher→m, menino→n, menina→i
 */
const char* VoiceManager::getVoicePath() const {
    switch (_voice) {
        case VOICE_HOMEM:  return "/a/h/";
        case VOICE_MULHER: return "/a/m/";
        case VOICE_MENINO: return "/a/n/";
        case VOICE_MENINA: return "/a/i/";
        default:           return "/a/h/";
    }
}

const char* VoiceManager::getVoiceName() const {
    switch (_voice) {
        case VOICE_HOMEM:  return "Homem";
        case VOICE_MULHER: return "Mulher";
        case VOICE_MENINO: return "Menino";
        case VOICE_MENINA: return "Menina";
        default:           return "Homem";
    }
}

// ============================================================================
// Fix UX12: Silent error mode methods
// ============================================================================

bool VoiceManager::getSilentErrors() const {
    return _silentErrors;
}

void VoiceManager::setSilentErrors(bool silent) {
    _silentErrors = silent;

    // Persist immediately to NVS
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putBool("silent_err", _silentErrors);
        prefs.end();
        Serial.printf("[%s] Silent errors %s (saved to NVS)\n",
                      TAG, _silentErrors ? "enabled" : "disabled");
    } else {
        Serial.printf("[%s] Failed to save silent_errors to NVS\n", TAG);
    }
}

// ============================================================================
// Gesture level methods
// ============================================================================

GestureLevel VoiceManager::getGestureLevel() const {
    return _gestureLevel;
}

void VoiceManager::setGestureLevel(GestureLevel level) {
    if (level >= LEVEL_COUNT) {
        Serial.printf("[%s] Invalid gesture level %d, ignoring\n", TAG, static_cast<int>(level));
        return;
    }
    _gestureLevel = level;

    // Persist immediately to NVS
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putUChar("gest_level", static_cast<uint8_t>(_gestureLevel));
        prefs.end();
        Serial.printf("[%s] Gesture level set to %d (saved to NVS)\n",
                      TAG, static_cast<int>(_gestureLevel));
    } else {
        Serial.printf("[%s] Failed to save gesture level to NVS\n", TAG);
    }
}

const LevelConfig& VoiceManager::getLevelConfig() const {
    return LEVEL_CONFIGS[_gestureLevel];
}

// ============================================================================
// Device name methods
// ============================================================================

const char* VoiceManager::getDeviceName() const {
    return _deviceName;
}

void VoiceManager::setDeviceName(const char* name) {
    if (name == nullptr || strlen(name) == 0) {
        Serial.printf("[%s] Invalid device name, ignoring\n", TAG);
        return;
    }

    strncpy(_deviceName, name, sizeof(_deviceName) - 1);
    _deviceName[sizeof(_deviceName) - 1] = '\0';

    // Persist immediately
    Preferences prefs;
    if (prefs.begin(NVS_NAMESPACE, false)) {
        prefs.putString("dev_name", _deviceName);
        prefs.end();
        Serial.printf("[%s] Device name saved to NVS: %s\n", TAG, _deviceName);
    } else {
        Serial.printf("[%s] Failed to save device name to NVS\n", TAG);
    }
}

// ============================================================================
// Profile methods
// ============================================================================

bool VoiceManager::isProfileActive(Profile profile) const {
    if (profile >= PROFILE_COUNT) {
        return false;
    }
    return _activeProfiles[profile];
}

void VoiceManager::activateProfile(Profile profile) {
    if (profile >= PROFILE_COUNT) {
        Serial.printf("[%s] Invalid profile %d\n", TAG, static_cast<int>(profile));
        return;
    }

    if (profile == PROFILE_BASE || profile == PROFILE_AUTOMACAO) {
        // Base and Automation profiles are always active
        return;
    }

    if (_activeProfiles[profile]) {
        Serial.printf("[%s] Profile %d already active\n", TAG, static_cast<int>(profile));
        return;
    }

    // Check max active profiles limit (excluding base and automacao)
    uint8_t count = 0;
    for (uint8_t i = 1; i < PROFILE_COUNT; i++) {  // Skip PROFILE_BASE
        if (i == PROFILE_AUTOMACAO) continue;       // Skip PROFILE_AUTOMACAO
        if (_activeProfiles[i]) {
            count++;
        }
    }

    if (count >= MAX_ACTIVE_PROFILES) {
        Serial.printf("[%s] Cannot activate profile %d: max %d active profiles reached\n",
                      TAG, static_cast<int>(profile), MAX_ACTIVE_PROFILES);
        return;
    }

    _activeProfiles[profile] = true;
    Serial.printf("[%s] Profile %d activated\n", TAG, static_cast<int>(profile));
}

void VoiceManager::deactivateProfile(Profile profile) {
    if (profile >= PROFILE_COUNT) {
        return;
    }

    if (profile == PROFILE_BASE || profile == PROFILE_AUTOMACAO) {
        // Cannot deactivate base or automation profile
        return;
    }

    _activeProfiles[profile] = false;
    Serial.printf("[%s] Profile %d deactivated\n", TAG, static_cast<int>(profile));
}

uint8_t VoiceManager::getActiveProfileCount() const {
    uint8_t count = 0;
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        if (_activeProfiles[i]) {
            count++;
        }
    }
    return count;
}

// ============================================================================
// Persistence
// ============================================================================

void VoiceManager::saveToFlash() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {
        Serial.printf("[%s] Failed to open NVS for writing\n", TAG);
        return;
    }

    prefs.putUChar("voice", static_cast<uint8_t>(_voice));
    prefs.putUChar("gest_level", static_cast<uint8_t>(_gestureLevel));

    // Save active profiles as a bitmask
    uint8_t profileMask = 0;
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        if (_activeProfiles[i]) {
            profileMask |= (1 << i);
        }
    }
    prefs.putUChar("profiles", profileMask);

    // Save device name
    prefs.putString("dev_name", _deviceName);

    // Fix UX12: Save silent errors setting
    prefs.putBool("silent_err", _silentErrors);

    prefs.end();
    Serial.printf("[%s] Settings saved to NVS\n", TAG);
}

void VoiceManager::loadFromFlash() {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {
        Serial.printf("[%s] No saved preferences found, using defaults\n", TAG);
        return;
    }

    // Load voice with validation
    uint8_t savedVoice = prefs.getUChar("voice", static_cast<uint8_t>(VOICE_HOMEM));
    if (savedVoice < VOICE_COUNT) {
        _voice = static_cast<Voice>(savedVoice);
    } else {
        _voice = VOICE_HOMEM;
    }

    // Load gesture level with validation
    uint8_t savedLevel = prefs.getUChar("gest_level", static_cast<uint8_t>(LEVEL_STANDARD));
    if (savedLevel < LEVEL_COUNT) {
        _gestureLevel = static_cast<GestureLevel>(savedLevel);
    } else {
        _gestureLevel = LEVEL_STANDARD;
    }

    // Load active profiles from bitmask
    uint8_t profileMask = prefs.getUChar("profiles", 0x01);  // Default: only base active
    for (uint8_t i = 0; i < PROFILE_COUNT; i++) {
        _activeProfiles[i] = (profileMask & (1 << i)) != 0;
    }
    // Ensure base profile is always active
    _activeProfiles[PROFILE_BASE] = true;
    // Ensure automation profile is always active
    _activeProfiles[PROFILE_AUTOMACAO] = true;

    // Load device name (if saved)
    String savedName = prefs.getString("dev_name", "");
    if (savedName.length() > 0 && savedName.length() < sizeof(_deviceName)) {
        strncpy(_deviceName, savedName.c_str(), sizeof(_deviceName) - 1);
        _deviceName[sizeof(_deviceName) - 1] = '\0';
    }
    // If no saved name, keep the MAC-based default set in generateDefaultName()

    // Fix UX12: Load silent errors setting
    _silentErrors = prefs.getBool("silent_err", false);

    prefs.end();
    Serial.printf("[%s] Settings loaded from NVS\n", TAG);
}
