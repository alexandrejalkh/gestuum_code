/**
 * GESTUUM — Central Configuration Command Handler Implementation
 * Responsibility: Parse JSON commands and dispatch to appropriate handlers.
 *
 * JSON command format:
 *   { "cmd": "command_name", "param1": value1, ... }
 *
 * JSON response format:
 *   { "status": "ok"|"error", "cmd": "command_name", ... }
 *
 * Uses ArduinoJson for parsing and serialization.
 * Base64 decoding for audio upload chunks uses a simple inline decoder.
 *
 * Fix B1: Added add_custom_gesture, remove_custom_gesture, and
 *         list_custom_gestures commands so uploaded audio files can
 *         be associated with gesture entries in /data/gestures/custom.json.
 */

#include "config_handler.h"
#include "config.h"  // Sprint C3c: GESTUUM_FW_VERSION centralizado
#include "voice_manager.h"
#include "gesture_engine.h"
#include "audio_player.h"
#include "espnow_comm.h"
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp_system.h>
#include "bt_audio.h"

// === Global instance ===
ConfigHandler configHandler;

// Firmware version string movida pra config.h (Sprint C3c, 2026-05-02)
// pra ser compartilhada com menu_ui (tela "Sobre").

static const char* TAG = "ConfigHandler";

// ============================================================================
// Base64 decode helper (inline, no external dependency)
// ============================================================================

static const char b64chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int b64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;  // '=' or invalid
}

/**
 * Decode base64 string into output buffer.
 * @param input   Null-terminated base64 string
 * @param output  Output buffer (must be large enough)
 * @param maxLen  Maximum output buffer size
 * @return Number of decoded bytes, or -1 on error
 */
static int base64_decode(const char* input, uint8_t* output, int maxLen) {
    int inputLen = strlen(input);
    if (inputLen % 4 != 0) {
        return -1;  // Invalid base64 length
    }

    int outPos = 0;
    for (int i = 0; i < inputLen; i += 4) {
        int a = b64_decode_char(input[i]);
        int b = b64_decode_char(input[i + 1]);
        int c = b64_decode_char(input[i + 2]);
        int d = b64_decode_char(input[i + 3]);

        if (a < 0 || b < 0) {
            return -1;  // Invalid character in first two positions
        }

        if (outPos >= maxLen) break;
        output[outPos++] = (uint8_t)((a << 2) | (b >> 4));

        if (input[i + 2] == '=') break;
        if (c < 0) return -1;
        if (outPos >= maxLen) break;
        output[outPos++] = (uint8_t)(((b & 0x0F) << 4) | (c >> 2));

        if (input[i + 3] == '=') break;
        if (d < 0) return -1;
        if (outPos >= maxLen) break;
        output[outPos++] = (uint8_t)(((c & 0x03) << 6) | d);
    }

    return outPos;
}

// ============================================================================
// begin()
// ============================================================================

void ConfigHandler::begin(VoiceManager* vm, GestureEngine* ge, AudioPlayer* ap, BTAudio* bt) {
    _voiceManager = vm;
    _gestureEngine = ge;
    _audioPlayer = ap;
    _btAudio = bt;
    _responseCb = nullptr;
    _uploading = false;
    _uploadSize = 0;
    _uploadReceived = 0;
    memset(_uploadFilename, 0, sizeof(_uploadFilename));

    // Training state init
    _trainingActive = false;
    memset(_trainingGestureId, 0, sizeof(_trainingGestureId));
    memset(_trainingGestureName, 0, sizeof(_trainingGestureName));
    _trainingStartCb = nullptr;
    _trainingCancelCb = nullptr;

    Serial.printf("[%s] Config handler initialized\n", TAG);
}

// ============================================================================
// setResponseCallback()
// ============================================================================

void ConfigHandler::setResponseCallback(ResponseCallback cb) {
    _responseCb = cb;
}

void ConfigHandler::setTrainingStartCallback(TrainingStartCallback cb) {
    _trainingStartCb = cb;
}

void ConfigHandler::setTrainingCancelCallback(TrainingCancelCallback cb) {
    _trainingCancelCb = cb;
}

// ============================================================================
// sendResponse() — Route response through the registered callback
// ============================================================================

void ConfigHandler::sendResponse(const char* json) {
    if (_responseCb != nullptr) {
        _responseCb(json);
    }
}

// ============================================================================
// handleCommand() — Parse JSON and dispatch to command handler
// ============================================================================

void ConfigHandler::handleCommand(const char* json) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json);

    if (err) {
        Serial.printf("[%s] JSON parse error: %s\n", TAG, err.c_str());

        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"msg\":\"JSON parse error: %s\"}", err.c_str());
        sendResponse(response);
        return;
    }

    const char* cmd = doc["cmd"] | "";

    if (strlen(cmd) == 0) {
        sendResponse("{\"status\":\"error\",\"msg\":\"Missing cmd field\"}");
        return;
    }

    Serial.printf("[%s] Processing command: %s\n", TAG, cmd);

    // Dispatch to command handlers
    if (strcmp(cmd, "get_config") == 0) {
        cmdGetConfig();
    }
    else if (strcmp(cmd, "set_voice") == 0) {
        int voice = doc["voice"] | -1;
        if (voice < 0 || voice >= VOICE_COUNT) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"set_voice\",\"msg\":\"Invalid voice\"}");
        } else {
            cmdSetVoice(voice);
        }
    }
    else if (strcmp(cmd, "set_profile") == 0) {
        int profile = doc["profile"] | -1;
        bool active = doc["active"] | false;
        if (profile < 0 || profile >= PROFILE_COUNT) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"set_profile\",\"msg\":\"Invalid profile\"}");
        } else {
            cmdSetProfile(profile, active);
        }
    }
    else if (strcmp(cmd, "set_name") == 0) {
        const char* name = doc["name"] | "";
        if (strlen(name) == 0 || strlen(name) > 20) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"set_name\",\"msg\":\"Invalid name (1-20 chars)\"}");
        } else {
            cmdSetName(name);
        }
    }
    else if (strcmp(cmd, "bt_discover") == 0) {
        cmdBtDiscover();
    }
    else if (strcmp(cmd, "bt_status") == 0) {
        cmdBtStatus();
    }
    else if (strcmp(cmd, "test_audio") == 0) {
        const char* filename = doc["filename"] | "";
        if (strlen(filename) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"test_audio\",\"msg\":\"Missing filename\"}");
        } else {
            cmdTestAudio(filename);
        }
    }
    else if (strcmp(cmd, "upload_start") == 0) {
        const char* filename = doc["filename"] | "";
        int size = doc["size"] | 0;
        if (strlen(filename) == 0 || size <= 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"upload_start\",\"msg\":\"Missing filename or size\"}");
        } else {
            cmdUploadStart(filename, size);
        }
    }
    else if (strcmp(cmd, "upload_chunk") == 0) {
        int offset = doc["offset"] | -1;
        const char* data = doc["data"] | "";
        if (offset < 0 || strlen(data) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Missing offset or data\"}");
        } else {
            cmdUploadChunk(offset, data);
        }
    }
    else if (strcmp(cmd, "upload_end") == 0) {
        const char* filename = doc["filename"] | "";
        if (strlen(filename) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"upload_end\",\"msg\":\"Missing filename\"}");
        } else {
            cmdUploadEnd(filename);
        }
    }
    else if (strcmp(cmd, "list_gestures") == 0) {
        cmdListGestures();
    }
    else if (strcmp(cmd, "get_profiles") == 0) {
        cmdGetProfiles();
    }
    else if (strcmp(cmd, "add_custom_gesture") == 0) {
        const char* name = doc["name"] | "";
        const char* audioFile = doc["audio_file"] | "";
        const char* category = doc["category"] | "GERAL";
        bool isSolo = doc["is_solo"] | false;
        int automationCmd = doc["automation_cmd"] | 0;

        if (strlen(name) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"add_custom_gesture\",\"msg\":\"Missing name\"}");
        } else if (strlen(audioFile) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"add_custom_gesture\",\"msg\":\"Missing audio_file\"}");
        } else {
            cmdAddCustomGesture(name, audioFile, category, isSolo, automationCmd);
        }
    }
    else if (strcmp(cmd, "remove_custom_gesture") == 0) {
        const char* name = doc["name"] | "";
        if (strlen(name) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\",\"msg\":\"Missing name\"}");
        } else {
            cmdRemoveCustomGesture(name);
        }
    }
    else if (strcmp(cmd, "list_custom_gestures") == 0) {
        cmdListCustomGestures();
    }
    else if (strcmp(cmd, "set_level") == 0) {
        int level = doc["level"] | -1;
        if (level < 0 || level >= LEVEL_COUNT) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"set_level\",\"msg\":\"Invalid level (0-2)\"}");
        } else {
            cmdSetLevel(level);
        }
    }
    else if (strcmp(cmd, "reset_training") == 0) {
        const char* gestureId = doc["gesture_id"] | "";
        if (strlen(gestureId) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"reset_training\",\"msg\":\"Missing gesture_id\"}");
        } else {
            bool ok = _gestureEngine->getLoader().resetGestureTraining(gestureId);
            if (ok) {
                // Recarregar gestos para refletir a mudanca
                _gestureEngine->loadAllCategories();
                char resp[128];
                snprintf(resp, sizeof(resp),
                    "{\"status\":\"ok\",\"cmd\":\"reset_training\",\"gesture_id\":\"%s\"}", gestureId);
                sendResponse(resp);
            } else {
                sendResponse("{\"status\":\"error\",\"cmd\":\"reset_training\",\"msg\":\"Gesture not found\"}");
            }
        }
    }
    else if (strcmp(cmd, "reboot") == 0) {
        cmdReboot();
    }
    else if (strcmp(cmd, "factory_reset") == 0) {
        cmdFactoryReset();
    }
    else if (strcmp(cmd, "set_channel") == 0) {
        int ch = doc["channel"] | -1;
        if (ch < 1 || ch > 13) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"set_channel\",\"msg\":\"Canal invalido (1-13)\"}");
        } else {
            char resp[128];
            snprintf(resp, sizeof(resp),
                     "{\"status\":\"ok\",\"cmd\":\"set_channel\",\"channel\":%d,\"msg\":\"Reiniciando todos os dispositivos...\"}", ch);
            sendResponse(resp);
            delay(200);  // Dar tempo para resposta chegar ao app
            espnow_set_channel(static_cast<uint8_t>(ch));  // Broadcast + NVS + restart
        }
    }
    else if (strcmp(cmd, "set_silent_errors") == 0) {
        bool value = doc["value"] | false;
        cmdSetSilentErrors(value);
    }
    else if (strcmp(cmd, "train_start") == 0) {
        const char* gestureId = doc["gesture_id"] | "";
        if (strlen(gestureId) == 0) {
            sendResponse("{\"status\":\"error\",\"cmd\":\"train_start\",\"msg\":\"Missing gesture_id\"}");
        } else {
            cmdTrainStart(gestureId);
        }
    }
    else if (strcmp(cmd, "train_cancel") == 0) {
        cmdTrainCancel();
    }
    else if (strcmp(cmd, "train_status") == 0) {
        cmdTrainStatus();
    }
    else if (strcmp(cmd, "train_sample_done") == 0) {
        cmdTrainSampleDone();
    }
    else if (strcmp(cmd, "train_save") == 0) {
        cmdTrainSave();
    }
    else if (strcmp(cmd, "list_trainable") == 0) {
        cmdListTrainableGestures();
    }
    else {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"msg\":\"Unknown command: %s\"}", cmd);
        sendResponse(response);
    }
}

// ============================================================================
// cmdGetConfig() — Return current device configuration
// ============================================================================

void ConfigHandler::cmdGetConfig() {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["cmd"] = "get_config";
    doc["fw_version"] = GESTUUM_FW_VERSION;
    doc["voice"] = static_cast<int>(_voiceManager->getCurrentVoice());
    doc["voice_name"] = _voiceManager->getVoiceName();
    doc["device_name"] = _voiceManager->getDeviceName();
    doc["silent_errors"] = _voiceManager->getSilentErrors();
    doc["level"] = static_cast<int>(_voiceManager->getGestureLevel());
    doc["gesture_count"] = _gestureEngine->getGestureCount();
    doc["category"] = static_cast<int>(_gestureEngine->getCurrentCategory());

    // Battery level (read from hardware)
    extern int getBatteryLevel();
    doc["battery"] = getBatteryLevel();

    // Active profiles
    JsonArray profiles = doc["profiles"].to<JsonArray>();
    for (int i = 0; i < PROFILE_COUNT; i++) {
        if (i == PROFILE_AUTOMACAO) continue;  // Automacao removida do MVP
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = i;
        p["active"] = _voiceManager->isProfileActive(static_cast<Profile>(i));
    }

    // SPIFFS info
    doc["spiffs_total"] = (int)SPIFFS.totalBytes();
    doc["spiffs_used"] = (int)SPIFFS.usedBytes();

    // FIX H12: Check de overflow — serializeJson pode exceder 512 bytes
    // com muitos profiles ativos. Truncamento gera JSON malformado.
    char response[512];
    int len = serializeJson(doc, response, sizeof(response));
    if (len >= (int)sizeof(response) - 1) {
        sendResponse("{\"status\":\"error\",\"msg\":\"Response too large\"}");
        return;
    }
    sendResponse(response);
}

// ============================================================================
// cmdSetVoice() — Change active voice
// ============================================================================

void ConfigHandler::cmdSetVoice(int voice) {
    _voiceManager->setVoice(static_cast<Voice>(voice));
    _voiceManager->saveToFlash();

    // Update audio player voice path
    _audioPlayer->setVoicePath(_voiceManager->getVoicePath());

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"set_voice\",\"voice\":%d,\"voice_name\":\"%s\"}",
             voice, _voiceManager->getVoiceName());
    sendResponse(response);

    Serial.printf("[%s] Voice changed to %s\n", TAG, _voiceManager->getVoiceName());
}

// ============================================================================
// cmdSetProfile() — Activate or deactivate a profile
// ============================================================================

void ConfigHandler::cmdSetProfile(int profile, bool active) {
    Profile p = static_cast<Profile>(profile);

    if (active) {
        _voiceManager->activateProfile(p);
    } else {
        _voiceManager->deactivateProfile(p);
    }
    _voiceManager->saveToFlash();

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"set_profile\",\"profile\":%d,\"active\":%s}",
             profile, active ? "true" : "false");
    sendResponse(response);

    Serial.printf("[%s] Profile %d %s\n", TAG, profile, active ? "activated" : "deactivated");
}

// ============================================================================
// cmdSetName() — Change BLE device name (persisted in NVS)
// ============================================================================

void ConfigHandler::cmdSetName(const char* name) {
    _voiceManager->setDeviceName(name);

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"set_name\",\"name\":\"%s\"}", name);
    sendResponse(response);

    Serial.printf("[%s] Device name changed to %s\n", TAG, name);
}

// ============================================================================
// cmdTestAudio() — Play an audio file for testing
// ============================================================================

void ConfigHandler::cmdTestAudio(const char* filename) {
    bool ok = _audioPlayer->playFile(filename);

    char response[128];
    if (ok) {
        snprintf(response, sizeof(response),
                 "{\"status\":\"ok\",\"cmd\":\"test_audio\",\"filename\":\"%s\"}", filename);
    } else {
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"test_audio\",\"msg\":\"File not found: %s\"}",
                 filename);
    }
    sendResponse(response);
}

// ============================================================================
// cmdUploadStart() — Begin audio file upload to SPIFFS
// ============================================================================

void ConfigHandler::cmdUploadStart(const char* filename, int size) {
    if (_uploading) {
        // Close any previous incomplete upload
        if (_uploadFile) {
            _uploadFile.close();
        }
        Serial.printf("[%s] Previous upload aborted\n", TAG);
    }

    // FIX H11: Validacao de path — previne escrita em arquivos criticos
    // Upload so e permitido em /audio/ (WAVs customizados)
    // Sem isso, um app bugado poderia sobrescrever geral.json ou qualquer
    // outro arquivo no SPIFFS
    if (strstr(filename, "..") != nullptr) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_start\",\"msg\":\"Invalid path: contains ..\"}");
        _uploading = false;
        return;
    }

    // Monta path absoluto
    if (filename[0] == '/') {
        strncpy(_uploadFilename, filename, sizeof(_uploadFilename) - 1);
    } else {
        snprintf(_uploadFilename, sizeof(_uploadFilename), "/audio/%s", filename);
    }
    _uploadFilename[sizeof(_uploadFilename) - 1] = '\0';

    // Valida que o path final esta dentro de /audio/
    if (strncmp(_uploadFilename, "/audio/", 7) != 0) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_start\",\"msg\":\"Upload only allowed in /audio/\"}");
        _uploading = false;
        return;
    }

    _uploadSize = size;
    _uploadReceived = 0;

    // Check available SPIFFS space
    int available = (int)(SPIFFS.totalBytes() - SPIFFS.usedBytes());
    if (size > available) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"upload_start\",\"msg\":\"Not enough space. Need %d, have %d\"}",
                 size, available);
        sendResponse(response);
        _uploading = false;
        return;
    }

    // Open file for writing
    _uploadFile = SPIFFS.open(_uploadFilename, FILE_WRITE);
    if (!_uploadFile) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"upload_start\",\"msg\":\"Failed to open file: %s\"}",
                 _uploadFilename);
        sendResponse(response);
        _uploading = false;
        return;
    }

    _uploading = true;

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"upload_start\",\"filename\":\"%s\",\"size\":%d}",
             _uploadFilename, size);
    sendResponse(response);

    Serial.printf("[%s] Upload started: %s (%d bytes)\n", TAG, _uploadFilename, size);
}

// ============================================================================
// cmdUploadChunk() — Receive and write a base64-encoded data chunk
// ============================================================================

void ConfigHandler::cmdUploadChunk(int offset, const char* base64data) {
    if (!_uploading || !_uploadFile) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"No upload in progress\"}");
        return;
    }

    // FIX M15: Validacao do offset — deve ser sequencial e dentro do tamanho declarado
    // Sem isso, um app bugado pode enviar chunks fora de ordem ou alem do tamanho,
    // corrompendo o arquivo ou causando writes inesperados no SPIFFS
    if (offset < 0 || offset >= _uploadSize) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Offset out of range\"}");
        return;
    }
    if (offset != _uploadReceived) {
        char errResp[128];
        snprintf(errResp, sizeof(errResp),
                 "{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Expected offset %d, got %d\"}",
                 _uploadReceived, offset);
        sendResponse(errResp);
        return;
    }

    // Decode base64 data
    // Max chunk size: 512 bytes decoded (base64 input ~684 chars)
    uint8_t decoded[512];
    int decodedLen = base64_decode(base64data, decoded, sizeof(decoded));

    if (decodedLen < 0) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Base64 decode error\"}");
        return;
    }

    // Seek to offset position
    if (!_uploadFile.seek(offset)) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Seek failed at offset %d\"}",
                 offset);
        sendResponse(response);
        return;
    }

    // Write decoded data
    size_t written = _uploadFile.write(decoded, decodedLen);
    if ((int)written != decodedLen) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"upload_chunk\",\"msg\":\"Write failed: %d/%d bytes\"}",
                 (int)written, decodedLen);
        sendResponse(response);
        return;
    }

    _uploadReceived += decodedLen;

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"upload_chunk\",\"offset\":%d,\"written\":%d,\"total_received\":%d}",
             offset, decodedLen, _uploadReceived);
    sendResponse(response);
}

// ============================================================================
// cmdUploadEnd() — Finalize audio file upload
// ============================================================================

void ConfigHandler::cmdUploadEnd(const char* filename) {
    if (!_uploading) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"upload_end\",\"msg\":\"No upload in progress\"}");
        return;
    }

    // Close the file
    _uploadFile.flush();
    _uploadFile.close();

    // Validate size
    bool sizeMatch = (_uploadReceived == _uploadSize);

    char response[256];
    if (sizeMatch) {
        snprintf(response, sizeof(response),
                 "{\"status\":\"ok\",\"cmd\":\"upload_end\",\"filename\":\"%s\",\"size\":%d}",
                 _uploadFilename, _uploadReceived);
        Serial.printf("[%s] Upload complete: %s (%d bytes)\n",
                      TAG, _uploadFilename, _uploadReceived);
    } else {
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"upload_end\",\"msg\":\"Size mismatch: expected %d, got %d\",\"filename\":\"%s\"}",
                 _uploadSize, _uploadReceived, _uploadFilename);
        Serial.printf("[%s] Upload size mismatch: %s (expected %d, got %d)\n",
                      TAG, _uploadFilename, _uploadSize, _uploadReceived);

        // Delete the incomplete file
        SPIFFS.remove(_uploadFilename);
    }

    _uploading = false;
    _uploadReceived = 0;
    _uploadSize = 0;
    memset(_uploadFilename, 0, sizeof(_uploadFilename));

    sendResponse(response);
}

// ============================================================================
// cmdListGestures() — List all loaded gestures
// ============================================================================

void ConfigHandler::cmdListGestures() {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["cmd"] = "list_gestures";
    doc["count"] = _gestureEngine->getGestureCount();
    doc["category"] = static_cast<int>(_gestureEngine->getCurrentCategory());

    JsonArray gestures = doc["gestures"].to<JsonArray>();
    int count = _gestureEngine->getGestureCount();

    for (int i = 0; i < count; i++) {
        const GestureDefinition& g = _gestureEngine->getGesture(i);
        JsonObject gObj = gestures.add<JsonObject>();
        gObj["index"] = i;
        gObj["name"] = g.name;
        gObj["audio"] = g.audioFile;
        gObj["solo"] = g.isSolo;
        gObj["automation"] = static_cast<int>(g.automationCmd);
    }

    // Serialize to buffer — gestures list can be large
    char response[2048];
    int len = serializeJson(doc, response, sizeof(response));

    if (len >= (int)sizeof(response) - 1) {
        // Response too large, send truncated warning
        sendResponse("{\"status\":\"error\",\"cmd\":\"list_gestures\",\"msg\":\"Response too large\"}");
    } else {
        sendResponse(response);
    }
}

// ============================================================================
// cmdGetProfiles() — Return profile activation status
// ============================================================================

void ConfigHandler::cmdGetProfiles() {
    JsonDocument doc;
    doc["status"] = "ok";
    doc["cmd"] = "get_profiles";

    JsonArray profiles = doc["profiles"].to<JsonArray>();
    for (int i = 0; i < PROFILE_COUNT; i++) {
        if (i == PROFILE_AUTOMACAO) continue;  // Automacao removida do MVP
        JsonObject p = profiles.add<JsonObject>();
        p["id"] = i;
        p["active"] = _voiceManager->isProfileActive(static_cast<Profile>(i));
    }

    char response[256];
    serializeJson(doc, response, sizeof(response));
    sendResponse(response);
}

// ============================================================================
// generateCustomId() — Generate next CUxx ID from existing custom.json
// ============================================================================

bool ConfigHandler::generateCustomId(char* outId, size_t maxLen) {
    int maxNum = 0;

    if (SPIFFS.exists(CUSTOM_GESTURES_PATH)) {
        File file = SPIFFS.open(CUSTOM_GESTURES_PATH, "r");
        if (file) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, file);
            file.close();

            if (!error) {
                JsonArrayConst arr = doc.as<JsonArrayConst>();
                if (!arr.isNull()) {
                    for (JsonObjectConst obj : arr) {
                        const char* id = obj["id"] | "";
                        // Parse CUxx format
                        if (strlen(id) >= 3 && id[0] == 'C' && id[1] == 'U') {
                            int num = atoi(id + 2);
                            if (num > maxNum) {
                                maxNum = num;
                            }
                        }
                    }
                }
            }
        }
    }

    int nextNum = maxNum + 1;
    if (nextNum > 99) {
        Serial.printf("[%s] Custom gesture ID overflow (max 99)\n", TAG);
        return false;
    }

    snprintf(outId, maxLen, "CU%02d", nextNum);
    return true;
}

// ============================================================================
// reloadCustomGestures() — Load custom.json into gesture engine
// ============================================================================

void ConfigHandler::reloadCustomGestures() {
    // Reload the current category to reset gesture list
    _gestureEngine->loadAllCategories();

    // Re-load any active profiles (they were cleared by loadGesturesForCategory)
    for (int i = 1; i < PROFILE_COUNT; i++) {
        Profile p = static_cast<Profile>(i);
        if (_voiceManager->isProfileActive(p)) {
            _gestureEngine->loadGesturesForProfile(p);
        }
    }

    // Append custom gestures from /data/gestures/custom.json
    _gestureEngine->loadCustomGestures();

    Serial.printf("[%s] Gesture engine reloaded with custom gestures\n", TAG);
}

// ============================================================================
// cmdAddCustomGesture() — Create a custom gesture entry in custom.json
// ============================================================================

void ConfigHandler::cmdAddCustomGesture(const char* name, const char* audioFile,
                                         const char* category, bool isSolo,
                                         int automationCmd) {
    // Generate the next custom ID
    char newId[8];
    if (!generateCustomId(newId, sizeof(newId))) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"add_custom_gesture\","
                     "\"msg\":\"Custom gesture ID limit reached (max 99)\"}");
        return;
    }

    // Check for duplicate name in existing custom gestures
    JsonDocument existingDoc;
    bool fileExists = SPIFFS.exists(CUSTOM_GESTURES_PATH);

    if (fileExists) {
        File file = SPIFFS.open(CUSTOM_GESTURES_PATH, "r");
        if (file) {
            DeserializationError error = deserializeJson(existingDoc, file);
            file.close();

            if (error) {
                // File corrupted, start fresh
                Serial.printf("[%s] Custom gestures file corrupted, creating new: %s\n",
                              TAG, error.c_str());
                existingDoc.to<JsonArray>();
            } else {
                // Check for duplicate name
                JsonArrayConst arr = existingDoc.as<JsonArrayConst>();
                if (!arr.isNull()) {
                    for (JsonObjectConst obj : arr) {
                        const char* existingName = obj["name"] | "";
                        if (strcmp(existingName, name) == 0) {
                            char response[256];
                            snprintf(response, sizeof(response),
                                     "{\"status\":\"error\",\"cmd\":\"add_custom_gesture\","
                                     "\"msg\":\"Gesture with name '%s' already exists\"}", name);
                            sendResponse(response);
                            return;
                        }
                    }
                }
            }
        } else {
            existingDoc.to<JsonArray>();
        }
    } else {
        existingDoc.to<JsonArray>();
    }

    // Build the gesture JSON object and append to the array
    JsonArray arr = existingDoc.as<JsonArray>();
    if (arr.isNull()) {
        // Root wasn't an array, reset
        existingDoc.to<JsonArray>();
        arr = existingDoc.as<JsonArray>();
    }

    JsonObject newGesture = arr.add<JsonObject>();
    newGesture["id"] = newId;
    newGesture["name"] = name;
    newGesture["category"] = category;
    newGesture["audio_file"] = audioFile;
    newGesture["threshold"] = DTW_THRESHOLD_DEFAULT;
    newGesture["is_solo"] = isSolo;
    newGesture["automation_cmd"] = automationCmd;
    newGesture["duration_ms"] = 1000;

    // Empty trajectories — gesture not yet trained, will be trained via app later
    JsonArray trajA = newGesture["trajectory_a"].to<JsonArray>();
    JsonArray trajB = newGesture["trajectory_b"].to<JsonArray>();
    // trajA and trajB remain empty arrays

    // Ensure parent directory exists (SPIFFS is flat, but path must be valid)
    // Write the updated array back to custom.json
    File outFile = SPIFFS.open(CUSTOM_GESTURES_PATH, FILE_WRITE);
    if (!outFile) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"add_custom_gesture\","
                     "\"msg\":\"Failed to write custom.json\"}");
        return;
    }

    serializeJson(existingDoc, outFile);
    outFile.close();

    Serial.printf("[%s] Custom gesture added: %s (%s) -> %s\n",
                  TAG, newId, name, audioFile);

    // Reload gesture engine with the new custom gesture
    reloadCustomGestures();

    // Send success response
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"add_custom_gesture\","
             "\"msg\":\"gesture_added\",\"id\":\"%s\",\"name\":\"%s\"}",
             newId, name);
    sendResponse(response);
}

// ============================================================================
// cmdRemoveCustomGesture() — Remove a custom gesture by name from custom.json
// ============================================================================

void ConfigHandler::cmdRemoveCustomGesture(const char* name) {
    if (!SPIFFS.exists(CUSTOM_GESTURES_PATH)) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                     "\"msg\":\"No custom gestures file found\"}");
        return;
    }

    // Read existing custom.json
    File file = SPIFFS.open(CUSTOM_GESTURES_PATH, "r");
    if (!file) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                     "\"msg\":\"Failed to open custom.json\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                     "\"msg\":\"Failed to parse custom.json\"}");
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                     "\"msg\":\"custom.json is not a valid array\"}");
        return;
    }

    // Find and remove the gesture by name
    bool found = false;
    for (size_t i = 0; i < arr.size(); i++) {
        const char* gestureName = arr[i]["name"] | "";
        if (strcmp(gestureName, name) == 0) {
            arr.remove(i);
            found = true;
            break;
        }
    }

    if (!found) {
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                 "\"msg\":\"Gesture '%s' not found in custom.json\"}", name);
        sendResponse(response);
        return;
    }

    // Write updated array back to file
    File outFile = SPIFFS.open(CUSTOM_GESTURES_PATH, FILE_WRITE);
    if (!outFile) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"remove_custom_gesture\","
                     "\"msg\":\"Failed to write custom.json\"}");
        return;
    }

    serializeJson(doc, outFile);
    outFile.close();

    Serial.printf("[%s] Custom gesture removed: %s\n", TAG, name);

    // Reload gesture engine without the removed gesture
    reloadCustomGestures();

    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"remove_custom_gesture\","
             "\"msg\":\"gesture_removed\",\"name\":\"%s\"}", name);
    sendResponse(response);
}

// ============================================================================
// cmdListCustomGestures() — List only custom gestures from custom.json
// ============================================================================

void ConfigHandler::cmdListCustomGestures() {
    JsonDocument responseDoc;
    responseDoc["status"] = "ok";
    responseDoc["cmd"] = "list_custom_gestures";

    JsonArray gestures = responseDoc["gestures"].to<JsonArray>();

    if (!SPIFFS.exists(CUSTOM_GESTURES_PATH)) {
        // No custom gestures file — return empty list
        responseDoc["count"] = 0;

        char response[256];
        serializeJson(responseDoc, response, sizeof(response));
        sendResponse(response);
        return;
    }

    File file = SPIFFS.open(CUSTOM_GESTURES_PATH, "r");
    if (!file) {
        responseDoc["count"] = 0;

        char response[256];
        serializeJson(responseDoc, response, sizeof(response));
        sendResponse(response);
        return;
    }

    JsonDocument fileDoc;
    DeserializationError error = deserializeJson(fileDoc, file);
    file.close();

    if (error) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"list_custom_gestures\","
                     "\"msg\":\"Failed to parse custom.json\"}");
        return;
    }

    JsonArrayConst arr = fileDoc.as<JsonArrayConst>();
    if (arr.isNull()) {
        responseDoc["count"] = 0;

        char response[256];
        serializeJson(responseDoc, response, sizeof(response));
        sendResponse(response);
        return;
    }

    int count = 0;
    for (JsonObjectConst obj : arr) {
        JsonObject gObj = gestures.add<JsonObject>();
        gObj["id"] = obj["id"] | "";
        gObj["name"] = obj["name"] | "";
        gObj["audio_file"] = obj["audio_file"] | "";
        gObj["category"] = obj["category"] | "GERAL";
        gObj["is_solo"] = obj["is_solo"] | false;
        gObj["automation_cmd"] = obj["automation_cmd"] | 0;
        gObj["trained"] = !obj["trajectory_a"].as<JsonArrayConst>().isNull()
                          && obj["trajectory_a"].as<JsonArrayConst>().size() > 0;
        count++;
    }

    responseDoc["count"] = count;

    char response[2048];
    int len = serializeJson(responseDoc, response, sizeof(response));

    if (len >= (int)sizeof(response) - 1) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"list_custom_gestures\","
                     "\"msg\":\"Response too large\"}");
    } else {
        sendResponse(response);
    }
}

// ============================================================================
// cmdSetLevel() — Set gesture capture level (sensitivity for motor abilities)
// ============================================================================

void ConfigHandler::cmdSetLevel(int level) {
    GestureLevel gl = static_cast<GestureLevel>(level);
    _voiceManager->setGestureLevel(gl);
    _gestureEngine->setLevelConfig(_voiceManager->getLevelConfig());

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"set_level\",\"level\":%d}", level);
    sendResponse(response);

    Serial.printf("[%s] Gesture level changed to %d\n", TAG, level);
}

// ============================================================================
// cmdSetSilentErrors() — Fix UX12: Enable/disable silent error mode
// ============================================================================

void ConfigHandler::cmdSetSilentErrors(bool value) {
    _voiceManager->setSilentErrors(value);
    _audioPlayer->setSilentErrors(value);

    char response[128];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"set_silent_errors\",\"value\":%s}",
             value ? "true" : "false");
    sendResponse(response);

    Serial.printf("[%s] Silent errors %s\n", TAG, value ? "enabled" : "disabled");
}

// ============================================================================
// isTrainingActive() — Check if training mode is active
// ============================================================================

bool ConfigHandler::isTrainingActive() const {
    return _trainingActive;
}

void ConfigHandler::resetTrainingState() {
    _trainingActive = false;
    memset(_trainingGestureId, 0, sizeof(_trainingGestureId));
    memset(_trainingGestureName, 0, sizeof(_trainingGestureName));
    Serial.printf("[%s] Training state reset by main\n", TAG);
}

// ============================================================================
// Helper: Convert string gesture ID (e.g. "G01") to numeric uint16_t
// Uses the same encoding as GestureLoader::parseGestureId.
// ============================================================================

static uint16_t parseGestureIdStr(const char* idStr) {
    if (idStr == nullptr || idStr[0] == '\0') return 0;

    uint8_t highByte = 0x01;
    const char* numStart = idStr;

    if (idStr[0] == 'G') {
        highByte = 0x01;
        numStart = idStr + 1;
    } else if (idStr[0] == 'C' && idStr[1] == 'X') {
        // Contextos: CX01 → 0xE001
        highByte = 0xE0;
        numStart = idStr + 2;
    } else if (idStr[0] == 'E' && idStr[1] == 'M') {
        highByte = 0x02;
        numStart = idStr + 2;
    } else if (idStr[0] == 'C' && idStr[1] == 'A') {
        highByte = 0x03;
        numStart = idStr + 2;
    } else if (idStr[0] == 'T' && idStr[1] == 'R') {
        highByte = 0x04;
        numStart = idStr + 2;
    } else if (idStr[0] == 'S' && idStr[1] == 'O') {
        highByte = 0x05;
        numStart = idStr + 2;
    } else if (idStr[0] == 'C' && idStr[1] == 'U') {
        highByte = 0x06;
        numStart = idStr + 2;
    } else if (idStr[0] == 'H' && idStr[1] == 'O') {
        highByte = 0x10;
        numStart = idStr + 2;
    } else if (idStr[0] == 'E' && idStr[1] == 'S') {
        highByte = 0x11;
        numStart = idStr + 2;
    } else if (idStr[0] == 'T' && idStr[1] == 'E') {
        highByte = 0x12;
        numStart = idStr + 2;
    } else if (idStr[0] == 'R' && idStr[1] == 'E') {
        highByte = 0x13;
        numStart = idStr + 2;
    } else if (idStr[0] == 'T' && idStr[1] == 'P') {
        highByte = 0x14;
        numStart = idStr + 2;
    } else if (idStr[0] == 'L' && idStr[1] == 'A') {
        highByte = 0x15;
        numStart = idStr + 2;
    }

    uint8_t lowByte = static_cast<uint8_t>(atoi(numStart));
    return static_cast<uint16_t>((highByte << 8) | lowByte);
}

// ============================================================================
// cmdTrainStart() — Begin gesture training for a given gesture_id
// ============================================================================

void ConfigHandler::cmdTrainStart(const char* gestureId) {
    // Check if already training
    if (_trainingActive) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"train_start\","
                 "\"msg\":\"Training already active for %s\"}", _trainingGestureId);
        sendResponse(response);
        return;
    }

    // Validate gesture_id exists in current gesture database
    uint16_t numericId = parseGestureIdStr(gestureId);
    int foundIndex = -1;

    for (int i = 0; i < _gestureEngine->getGestureCount(); i++) {
        if (_gestureEngine->getGesture(i).id == numericId) {
            foundIndex = i;
            break;
        }
    }

    if (foundIndex < 0) {
        char response[128];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"train_start\","
                 "\"msg\":\"Gesture %s not found in database\"}", gestureId);
        sendResponse(response);
        return;
    }

    // Set training state
    _trainingActive = true;
    strncpy(_trainingGestureId, gestureId, sizeof(_trainingGestureId) - 1);
    _trainingGestureId[sizeof(_trainingGestureId) - 1] = '\0';
    strncpy(_trainingGestureName, _gestureEngine->getGesture(foundIndex).name,
            sizeof(_trainingGestureName) - 1);
    _trainingGestureName[sizeof(_trainingGestureName) - 1] = '\0';

    // Clear previous training samples in gesture engine
    _gestureEngine->clearTrainingSamples();

    // Notify main.cpp to enter STATE_TRAINING on the state machine
    if (_trainingStartCb) {
        _trainingStartCb(_trainingGestureId, _trainingGestureName);
    }

    // FIX 2026-05-01: o callback acima pode RESETAR _trainingActive
    // (ex: ALT-17 em main.cpp:1543 bloqueia treino se Sensor B desconectado;
    // onTrainingStartCmd chama resetTrainingState()). Verificar se ainda
    // ativo antes de mentir "status:training" pro PWA.
    if (!_trainingActive) {
        char response[200];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"train_start\","
                 "\"msg\":\"Treino bloqueado: Sensor B desconectado ou outra falha\"}");
        sendResponse(response);
        Serial.printf("[%s] Training NAO iniciado para %s — bloqueado pelo callback\n",
                      TAG, gestureId);
        return;
    }

    // Respond to app
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"training\",\"cmd\":\"train_start\","
             "\"gesture\":\"%s\",\"samples_needed\":%d}",
             _trainingGestureName, 3);
    sendResponse(response);

    Serial.printf("[%s] Training started for gesture %s (%s)\n",
                  TAG, gestureId, _trainingGestureName);
}

// ============================================================================
// cmdTrainCancel() — Cancel active training session
// ============================================================================

void ConfigHandler::cmdTrainCancel() {
    if (!_trainingActive) {
        sendResponse("{\"status\":\"ok\",\"cmd\":\"train_cancel\",\"msg\":\"no_training_active\"}");
        return;
    }

    // Clear samples and reset state
    _gestureEngine->clearTrainingSamples();
    _trainingActive = false;
    memset(_trainingGestureId, 0, sizeof(_trainingGestureId));
    memset(_trainingGestureName, 0, sizeof(_trainingGestureName));

    // Notify main.cpp to cancel STATE_TRAINING
    if (_trainingCancelCb) {
        _trainingCancelCb();
    }

    sendResponse("{\"status\":\"ok\",\"cmd\":\"train_cancel\",\"msg\":\"training_cancelled\"}");

    Serial.printf("[%s] Training cancelled\n", TAG);
}

// ============================================================================
// cmdTrainStatus() — Query training progress
// ============================================================================

void ConfigHandler::cmdTrainStatus() {
    if (!_trainingActive) {
        sendResponse("{\"status\":\"idle\",\"cmd\":\"train_status\"}");
        return;
    }

    int samples = _gestureEngine->getTrainingSampleCount();
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"training\",\"cmd\":\"train_status\","
             "\"gesture\":\"%s\",\"samples\":%d,\"needed\":%d}",
             _trainingGestureName, samples, 3);
    sendResponse(response);
}

// ============================================================================
// cmdTrainSampleDone() — Called by main.cpp when a training sample is captured
// ============================================================================

void ConfigHandler::cmdTrainSampleDone() {
    if (!_trainingActive) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"train_sample_done\","
                     "\"msg\":\"No training active\"}");
        return;
    }

    // The sample should already have been added to the engine by main.cpp
    int samples = _gestureEngine->getTrainingSampleCount();

    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"sample_captured\",\"cmd\":\"train_sample_done\","
             "\"sample\":%d,\"needed\":%d}",
             samples, 3);
    sendResponse(response);

    Serial.printf("[%s] Training sample %d/%d captured for %s\n",
                  TAG, samples, 3, _trainingGestureName);
}

// ============================================================================
// notifyTrainingSampleCaptured() — Called by main.cpp after sample capture
// Sends event notification to the app via active transport.
// ============================================================================

void ConfigHandler::notifyTrainingSampleCaptured() {
    if (!_trainingActive) return;

    int samples = _gestureEngine->getTrainingSampleCount();

    // Send event notification to the app
    char event[128];
    snprintf(event, sizeof(event),
             "{\"event\":\"training_sample\",\"sample\":%d,\"total\":%d}",
             samples, 3);
    sendResponse(event);

    Serial.printf("[%s] Training sample notification sent: %d/%d\n",
                  TAG, samples, 3);
}

// ============================================================================
// notifyTrainingComplete() — Called by main.cpp when training is done
// Sends event notification to the app.
// ============================================================================

void ConfigHandler::notifyTrainingComplete() {
    if (!_trainingActive) return;

    // Compute the average gesture to get the threshold for the notification
    GestureDefinition avg = _gestureEngine->computeAverageGesture();

    char event[256];
    snprintf(event, sizeof(event),
             "{\"event\":\"training_complete\",\"gesture\":\"%s\",\"threshold\":%.1f}",
             _trainingGestureName, avg.threshold);
    sendResponse(event);

    Serial.printf("[%s] Training complete notification sent for %s (threshold=%.1f)\n",
                  TAG, _trainingGestureName, avg.threshold);
}

// ============================================================================
// cmdTrainSave() — Compute average and save trained gesture
// ============================================================================

void ConfigHandler::cmdTrainSave() {
    if (!_trainingActive) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"train_save\","
                     "\"msg\":\"No training active\"}");
        return;
    }

    if (!_gestureEngine->hasEnoughSamples()) {
        int samples = _gestureEngine->getTrainingSampleCount();
        char response[256];
        snprintf(response, sizeof(response),
                 "{\"status\":\"error\",\"cmd\":\"train_save\","
                 "\"msg\":\"Not enough samples: %d/%d\"}",
                 samples, 3);
        sendResponse(response);
        return;
    }

    // Compute average gesture via gesture engine
    GestureDefinition avg = _gestureEngine->computeAverageGesture();

    // Save trajectory via gesture_loader
    // FIX INSTALL-20: Salvar accel + gyro
    bool saved = _gestureEngine->getLoader().saveGestureTrajectory(
        _trainingGestureId,
        avg.trajectoryA,
        avg.trajectoryB,
        avg.durationMs,
        avg.threshold,
        avg.trajectoryAGyro,
        avg.trajectoryBGyro
    );

    if (!saved) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"train_save\","
                     "\"msg\":\"Failed to save gesture trajectory\"}");
        return;
    }

    float threshold = avg.threshold;

    // Reload gestures to pick up the new trajectory data
    reloadCustomGestures();

    // Clear training state
    char gestureName[32];
    strncpy(gestureName, _trainingGestureName, sizeof(gestureName) - 1);
    gestureName[sizeof(gestureName) - 1] = '\0';

    _gestureEngine->clearTrainingSamples();
    _trainingActive = false;
    memset(_trainingGestureId, 0, sizeof(_trainingGestureId));
    memset(_trainingGestureName, 0, sizeof(_trainingGestureName));

    // Respond to app
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"ok\",\"cmd\":\"train_save\","
             "\"msg\":\"gesture_trained\",\"gesture\":\"%s\",\"threshold\":%.1f}",
             gestureName, threshold);
    sendResponse(response);

    Serial.printf("[%s] Gesture %s trained and saved (threshold=%.1f)\n",
                  TAG, gestureName, threshold);
}

// ============================================================================
// cmdListTrainableGestures() — List all gestures with trained/untrained status
// ============================================================================

void ConfigHandler::cmdListTrainableGestures() {
    JsonDocument responseDoc;
    responseDoc["status"] = "gestures";
    responseDoc["cmd"] = "list_trainable";

    JsonArray list = responseDoc["list"].to<JsonArray>();
    int count = _gestureEngine->getGestureCount();

    // Build ID string lookup for each gesture
    // FIX (Sprint A 2026-05-01): contextos (0xE0xx) agora SAO incluidos na lista
    // pra o PWA poder treina-los. Antes eram filtrados, deixando a mao esquerda
    // sem treino possivel via PWA.
    //
    // NOTA AUDITOR (2026-05-02 — D-C0-01 em docs/pendencias_caminho_c.md):
    // Cases TR/HO/TE/RE/TP/LA sao prefixos "reservados pra v2" (perfis nao
    // implementados ou origem desconhecida). Mantidos por decisao explicita
    // do dono. JSONs correspondentes foram deletados em 2026-05-02 — gestos
    // com esses prefixos NAO sao carregados (getCategoryPath/getProfilePath
    // retornam nullptr). Dead code seguro mas semanticamente confuso.
    // TODO auditor: investigar TE/RE/LA, mapear origem ou remover.
    for (int i = 0; i < count; i++) {
        const GestureDefinition& g = _gestureEngine->getGesture(i);

        JsonObject item = list.add<JsonObject>();

        // Reconstruct string ID from numeric ID
        uint8_t high = static_cast<uint8_t>((g.id >> 8) & 0xFF);
        uint8_t low = static_cast<uint8_t>(g.id & 0xFF);
        char idStr[8];

        switch (high) {
            case 0x01: snprintf(idStr, sizeof(idStr), "G%02d", low); break;
            case 0x02: snprintf(idStr, sizeof(idStr), "EM%02d", low); break;
            case 0x03: snprintf(idStr, sizeof(idStr), "CA%02d", low); break;
            case 0x04: snprintf(idStr, sizeof(idStr), "TR%02d", low); break;   // [v2 reservado]
            case 0x05: snprintf(idStr, sizeof(idStr), "SO%02d", low); break;
            case 0x06: snprintf(idStr, sizeof(idStr), "CU%02d", low); break;
            case 0x10: snprintf(idStr, sizeof(idStr), "HO%02d", low); break;   // [v2 reservado]
            case 0x11: snprintf(idStr, sizeof(idStr), "ES%02d", low); break;
            case 0x12: snprintf(idStr, sizeof(idStr), "TE%02d", low); break;   // [origem desconhecida]
            case 0x13: snprintf(idStr, sizeof(idStr), "RE%02d", low); break;   // [origem desconhecida]
            case 0x14: snprintf(idStr, sizeof(idStr), "TP%02d", low); break;   // [v2 reservado]
            case 0x15: snprintf(idStr, sizeof(idStr), "LA%02d", low); break;   // [origem desconhecida]
            case 0xE0: snprintf(idStr, sizeof(idStr), "CX%02d", low); break;  // FIX: contextos
            default:   snprintf(idStr, sizeof(idStr), "?%02d", low); break;
        }

        item["id"] = idStr;
        item["name"] = g.name;
        item["trained"] = _gestureEngine->getLoader().isGestureTrained(idStr);
    }

    // FIX (Sprint A 2026-05-01): buffer 2048 -> 4096 pra caber a lista cheia
    // (78 gestos × ~30 bytes = ~2.4 KB + overhead). 2048 estourava com >50 gestos.
    char response[4096];
    int len = serializeJson(responseDoc, response, sizeof(response));

    if (len >= (int)sizeof(response) - 1) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"list_trainable\","
                     "\"msg\":\"Response too large\"}");
    } else {
        sendResponse(response);
    }
}

// ============================================================================
// cmdReboot() — Restart the device
// ============================================================================

void ConfigHandler::cmdReboot() {
    // FIX: Fechar upload se ativo antes de reiniciar
    abortUploadIfActive();

    sendResponse("{\"status\":\"ok\",\"cmd\":\"reboot\",\"msg\":\"Rebooting in 1 second...\"}");

    Serial.printf("[%s] Reboot requested, restarting...\n", TAG);
    delay(1000);
    ESP.restart();
}

// ============================================================================
// cmdFactoryReset() — Volta o dispositivo ao estado de fabrica
// ============================================================================

void ConfigHandler::cmdFactoryReset() {
    abortUploadIfActive();

    sendResponse("{\"status\":\"ok\",\"cmd\":\"factory_reset\",\"msg\":\"Restaurando configuracoes de fabrica...\"}");

    Serial.println("[ConfigHandler] FACTORY RESET — limpando NVS...");

    // Limpa TODAS as preferences do GESTUUM
    Preferences prefs;

    // VoiceManager namespace
    if (prefs.begin("gestuum_vm", false)) {
        prefs.clear();
        prefs.end();
    }

    // Canal ESP-NOW namespace
    if (prefs.begin("gestuum_ch", false)) {
        prefs.clear();
        prefs.end();
    }

    Serial.println("[ConfigHandler] NVS limpo. Reiniciando...");

    delay(500);
    ESP.restart();
}

// ============================================================================
// abortUploadIfActive() — Abortar upload em andamento de forma segura
// ============================================================================

void ConfigHandler::abortUploadIfActive() {
    if (_uploading) {
        Serial.printf("[%s] Aborting active upload: %s\n", TAG, _uploadFilename);
        if (_uploadFile) {
            _uploadFile.close();
        }
        // Remove arquivo parcial corrompido
        if (SPIFFS.exists(_uploadFilename)) {
            SPIFFS.remove(_uploadFilename);
            Serial.printf("[%s] Removed partial file: %s\n", TAG, _uploadFilename);
        }
        _uploading = false;
        _uploadReceived = 0;
        _uploadSize = 0;
        _uploadFilename[0] = '\0';
    }
}

// ============================================================================
// Bluetooth A2DP commands
// ============================================================================

void ConfigHandler::cmdBtDiscover() {
    if (_btAudio == nullptr) {
        sendResponse("{\"status\":\"error\",\"cmd\":\"bt_discover\",\"msg\":\"BT not initialized\"}");
        return;
    }

    sendResponse("{\"status\":\"ok\",\"cmd\":\"bt_discover\",\"msg\":\"Searching for BT speaker...\"}");

    // Busca bloqueante por ate 15 segundos
    bool found = _btAudio->discoverAndConnect(15000);

    char buf[128];
    if (found) {
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"ok\",\"cmd\":\"bt_discover\",\"msg\":\"Connected!\",\"connected\":true}");
    } else {
        snprintf(buf, sizeof(buf),
                 "{\"status\":\"ok\",\"cmd\":\"bt_discover\",\"msg\":\"No speaker found\",\"connected\":false}");
    }
    sendResponse(buf);
}

void ConfigHandler::cmdBtStatus() {
    if (_btAudio == nullptr) {
        sendResponse("{\"status\":\"ok\",\"cmd\":\"bt_status\",\"connected\":false,\"initialized\":false}");
        return;
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"status\":\"ok\",\"cmd\":\"bt_status\",\"connected\":%s,\"initialized\":true}",
             _btAudio->isConnected() ? "true" : "false");
    sendResponse(buf);
}
