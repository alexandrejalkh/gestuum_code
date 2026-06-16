/**
 * GESTUUM — Gesture Loader Implementation
 * Bloco: 5.1 — Gesture JSON Database + GestureLoader
 * Responsibility: Load and parse gesture definitions from JSON files on SPIFFS.
 *                 Uses ArduinoJson v7 (unified JsonDocument, no Static/Dynamic variants).
 *
 * Fix H1: Validate trajectory point values are within GRID_MIN..GRID_MAX (0-10)
 *         before casting to int8_t. Skip invalid points with a warning.
 * Fix M6: Validate required fields (id, name, category).
 *         audio_file may be empty for automation-only gestures.
 */

#include "gesture_loader.h"
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <cstring>

// ============================================================================
// Constructor
// ============================================================================

GestureLoader::GestureLoader()
    : spiffsReady(false) {
}

// ============================================================================
// begin() — Initialize SPIFFS
// ============================================================================

bool GestureLoader::begin() {
    if (spiffsReady) {
        return true;
    }

    // FIX A7: begin(false) primeiro — nao formatar automaticamente.
    // begin(true) apaga todos os gestos treinados silenciosamente em caso de corrupcao.
    if (!SPIFFS.begin(false)) {
        Serial.println("[GestureLoader] SPIFFS mount failed, tentando format...");
        if (!SPIFFS.begin(true)) {
            Serial.println("[GestureLoader] SPIFFS format+mount FAILED");
            return false;
        }
        Serial.println("[GestureLoader] SPIFFS formatado — gestos anteriores PERDIDOS");
    }

    spiffsReady = true;
    Serial.println("[GestureLoader] SPIFFS mounted OK");
    return true;
}

// ============================================================================
// getCategoryPath() — Map category enum to SPIFFS file path
// ============================================================================

/**
 * Retorna o path SPIFFS do JSON de gestos para a categoria.
 *
 * FIX: Paths encurtados de /data/gestures/ para /g/.
 * SPIFFS no ESP32 tem limite de 31 chars por path.
 * /data/gestures/emergencia.json = 30 chars (no limite)
 * /g/emergencia.json = 20 chars (folga de 11 chars)
 */
const char* GestureLoader::getCategoryPath(Category cat) {
    switch (cat) {
        case CAT_GERAL:      return "/g/geral.json";
        case CAT_EMERGENCIA: return "/g/emergencia.json";
        case CAT_CASA:       return "/g/casa.json";
        // FIX 2026-05-02: trabalho.json era fantasma (10 gestos sem audio,
        // perfil v2 nunca implementado). Arquivo deletado em
        // sensor_a/data/g/trabalho.json. Enum CAT_TRABALHO mantido pra
        // retrocompat no resto do codigo. Loader retorna nullptr aqui pra
        // loadAllCategories() pular silenciosamente.
        case CAT_TRABALHO:   return nullptr;
        case CAT_SOCIAL:     return "/g/social.json";
        default:             return nullptr;
    }
}

// ============================================================================
// getProfilePath() — Map profile enum to SPIFFS file path
// ============================================================================

const char* GestureLoader::getProfilePath(Profile profile) {
    switch (profile) {
        // FIX 2026-05-02: hospital.json e transporte.json eram fantasmas
        // (perfis v2 nao implementados). Arquivos deletados em
        // sensor_a/data/g/. Enums mantidos pra retrocompat.
        case PROFILE_HOSPITAL:    return nullptr;
        case PROFILE_ESCOLA:      return "/g/escola.json";
        case PROFILE_TRANSPORTE:  return nullptr;
        case PROFILE_AUTOMACAO:   return "/g/automacao.json";
        default:                  return nullptr;
    }
}

// ============================================================================
// parseGestureId() — Convert string ID to numeric ID
// ============================================================================

uint16_t GestureLoader::parseGestureId(const char* idStr) {
    if (idStr == nullptr || idStr[0] == '\0') {
        return 0;
    }

    // Determine category prefix and extract numeric part
    // G01-G10 -> 0x01XX, EM01-EM10 -> 0x02XX, CA01-CA10 -> 0x03XX,
    // TR01-TR10 -> 0x04XX, SO01-SO10 -> 0x05XX
    uint8_t highByte = 0x01;
    const char* numStart = idStr;

    if (idStr[0] == 'G') {
        highByte = 0x01;
        numStart = idStr + 1;
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
    }

    uint8_t lowByte = static_cast<uint8_t>(atoi(numStart));
    return static_cast<uint16_t>((highByte << 8) | lowByte);
}

// ============================================================================
// parseCategoryStr() — Convert category string to enum
// ============================================================================

Category GestureLoader::parseCategoryStr(const char* catStr) {
    if (catStr == nullptr) return CAT_GERAL;

    if (strcmp(catStr, "GERAL") == 0)      return CAT_GERAL;
    if (strcmp(catStr, "EMERGENCIA") == 0)  return CAT_EMERGENCIA;
    if (strcmp(catStr, "CASA") == 0)        return CAT_CASA;
    if (strcmp(catStr, "TRABALHO") == 0)    return CAT_TRABALHO;
    if (strcmp(catStr, "SOCIAL") == 0)      return CAT_SOCIAL;

    return CAT_GERAL;
}

// ============================================================================
// parseGestureObj() — Parse a single JSON gesture object
// ============================================================================

bool GestureLoader::parseGestureObj(const void* objPtr, GestureDefinition& gesture) {
    // Cast from void* to JsonObjectConst (avoids exposing ArduinoJson in header)
    const JsonObjectConst& obj = *static_cast<const JsonObjectConst*>(objPtr);

    // Required fields
    const char* idStr = obj["id"] | "";
    const char* name = obj["name"] | "";
    const char* catStr = obj["category"] | "";
    const char* audioFile = obj["audio_file"] | "";

    // Fix M6: Validate required fields (id, name, category must be non-empty)
    if (strlen(idStr) == 0) {
        Serial.println("[GestureLoader] WARNING: Skipping gesture with empty 'id'");
        return false;
    }
    if (strlen(name) == 0) {
        Serial.println("[GestureLoader] WARNING: Skipping gesture with empty 'name'");
        return false;
    }
    if (strlen(catStr) == 0) {
        Serial.printf("[GestureLoader] WARNING: Skipping gesture '%s' with empty 'category'\n", idStr);
        return false;
    }
    // audio_file may be empty for automation-only gestures — no longer a hard requirement

    gesture.id = parseGestureId(idStr);
    // Sprint C2 (Caminho C, 2026-05-02): preservar string ID original.
    // Necessario porque o menu local chama startTrainingMode(idStr,...) e
    // o save em saveGestureTrajectory(idStr,...) usa o prefix pra mapear file.
    // Sem isso, teriamos que reverter uint16 -> string com tabela de prefixos.
    strncpy(gesture.idStr, idStr, sizeof(gesture.idStr) - 1);
    gesture.idStr[sizeof(gesture.idStr) - 1] = '\0';
    gesture.category = parseCategoryStr(catStr);
    gesture.threshold = obj["threshold"] | DTW_THRESHOLD_DEFAULT;
    gesture.isSolo = obj["is_solo"] | false;
    // FIX 2026-05-16 (bug-hunter sprint F audit): faltava popular isContext.
    // Menu local rebuildFilteredList(HF_LEFT) filtrava por g.isContext mas
    // parseGestureObj nunca lia esse campo -> mao Esquerda sempre vazia.
    gesture.isContext = obj["is_context"] | false;
    gesture.automationCmd = static_cast<AutomationCmd>(obj["automation_cmd"] | 0);
    gesture.durationMs = obj["duration_ms"] | static_cast<uint16_t>(1000);
    gesture.trained = obj["trained"] | false;  // FIX INSTALL-17: false = placeholder, skip no match

    strncpy(gesture.name, name, sizeof(gesture.name) - 1);
    gesture.name[sizeof(gesture.name) - 1] = '\0';

    strncpy(gesture.audioFile, audioFile, sizeof(gesture.audioFile) - 1);
    gesture.audioFile[sizeof(gesture.audioFile) - 1] = '\0';

    // Parse trajectory A with grid bounds validation (Fix H1)
    gesture.trajectoryA.clear();
    JsonArrayConst trajA = obj["trajectory_a"];
    if (!trajA.isNull()) {
        for (JsonArrayConst point : trajA) {
            if (point.size() >= 3) {
                int valX = point[0].as<int>();
                int valY = point[1].as<int>();
                int valZ = point[2].as<int>();

                // Fix H1: Validate each coordinate is within GRID_MIN..GRID_MAX
                if (valX < GRID_MIN || valX > GRID_MAX ||
                    valY < GRID_MIN || valY > GRID_MAX ||
                    valZ < GRID_MIN || valZ > GRID_MAX) {
                    Serial.printf("[GestureLoader] WARNING: Gesture '%s' trajectory_a point [%d,%d,%d] "
                                  "out of range [%d..%d], skipping point\n",
                                  idStr, valX, valY, valZ, GRID_MIN, GRID_MAX);
                    continue;
                }

                int8_t x = static_cast<int8_t>(valX);
                int8_t y = static_cast<int8_t>(valY);
                int8_t z = static_cast<int8_t>(valZ);
                gesture.trajectoryA.push_back(Point3D(x, y, z));
            }
        }
    }

    // Parse trajectory B with grid bounds validation (Fix H1)
    gesture.trajectoryB.clear();
    JsonArrayConst trajB = obj["trajectory_b"];
    if (!trajB.isNull()) {
        for (JsonArrayConst point : trajB) {
            if (point.size() >= 3) {
                int valX = point[0].as<int>();
                int valY = point[1].as<int>();
                int valZ = point[2].as<int>();

                // Fix H1: Validate each coordinate is within GRID_MIN..GRID_MAX
                if (valX < GRID_MIN || valX > GRID_MAX ||
                    valY < GRID_MIN || valY > GRID_MAX ||
                    valZ < GRID_MIN || valZ > GRID_MAX) {
                    Serial.printf("[GestureLoader] WARNING: Gesture '%s' trajectory_b point [%d,%d,%d] "
                                  "out of range [%d..%d], skipping point\n",
                                  idStr, valX, valY, valZ, GRID_MIN, GRID_MAX);
                    continue;
                }

                int8_t x = static_cast<int8_t>(valX);
                int8_t y = static_cast<int8_t>(valY);
                int8_t z = static_cast<int8_t>(valZ);
                gesture.trajectoryB.push_back(Point3D(x, y, z));
            }
        }
    }

    // FIX INSTALL-20: Parse gyro trajectories (optional — may not exist in old JSONs)
    gesture.trajectoryAGyro.clear();
    JsonArrayConst trajAG = obj["trajectory_a_gyro"];
    if (!trajAG.isNull()) {
        for (JsonArrayConst point : trajAG) {
            if (point.size() >= 3) {
                int vx = point[0].as<int>();
                int vy = point[1].as<int>();
                int vz = point[2].as<int>();
                if (vx >= GRID_MIN && vx <= GRID_MAX &&
                    vy >= GRID_MIN && vy <= GRID_MAX &&
                    vz >= GRID_MIN && vz <= GRID_MAX) {
                    gesture.trajectoryAGyro.push_back(Point3D(
                        static_cast<int8_t>(vx), static_cast<int8_t>(vy), static_cast<int8_t>(vz)));
                }
            }
        }
    }

    gesture.trajectoryBGyro.clear();
    JsonArrayConst trajBG = obj["trajectory_b_gyro"];
    if (!trajBG.isNull()) {
        for (JsonArrayConst point : trajBG) {
            if (point.size() >= 3) {
                int vx = point[0].as<int>();
                int vy = point[1].as<int>();
                int vz = point[2].as<int>();
                if (vx >= GRID_MIN && vx <= GRID_MAX &&
                    vy >= GRID_MIN && vy <= GRID_MAX &&
                    vz >= GRID_MIN && vz <= GRID_MAX) {
                    gesture.trajectoryBGyro.push_back(Point3D(
                        static_cast<int8_t>(vx), static_cast<int8_t>(vy), static_cast<int8_t>(vz)));
                }
            }
        }
    }

    // === Modelo Orbital: carregar assinatura orbital (se existir no JSON) ===
    JsonObjectConst sigA = obj["sig_a"];
    if (!sigA.isNull()) {
        gesture.signatureA.amplitude   = sigA["amp"]    | 0.0f;
        gesture.signatureA.peak        = sigA["peak"]   | 0.0f;
        gesture.signatureA.linearity   = sigA["lin"]    | 0.0f;
        gesture.signatureA.duration    = sigA["dur"]    | 0.0f;
        gesture.signatureA.smoothness  = sigA["sm"]     | 0.0f;
        gesture.signatureA.rotation    = sigA["rot"]    | 0.0f;
        gesture.signatureA.symmetry    = sigA["sym"]    | 0.0f;
        JsonArrayConst nArr = sigA["n"];
        if (!nArr.isNull() && nArr.size() >= 3) {
            gesture.signatureA.planeNormal[0] = nArr[0].as<float>();
            gesture.signatureA.planeNormal[1] = nArr[1].as<float>();
            gesture.signatureA.planeNormal[2] = nArr[2].as<float>();
        }
        gesture.signatureA.valid = true;
    }

    JsonObjectConst sigB = obj["sig_b"];
    if (!sigB.isNull()) {
        gesture.signatureB.amplitude   = sigB["amp"]    | 0.0f;
        gesture.signatureB.peak        = sigB["peak"]   | 0.0f;
        gesture.signatureB.linearity   = sigB["lin"]    | 0.0f;
        gesture.signatureB.duration    = sigB["dur"]    | 0.0f;
        gesture.signatureB.smoothness  = sigB["sm"]     | 0.0f;
        gesture.signatureB.rotation    = sigB["rot"]    | 0.0f;
        gesture.signatureB.symmetry    = sigB["sym"]    | 0.0f;
        JsonArrayConst nArr = sigB["n"];
        if (!nArr.isNull() && nArr.size() >= 3) {
            gesture.signatureB.planeNormal[0] = nArr[0].as<float>();
            gesture.signatureB.planeNormal[1] = nArr[1].as<float>();
            gesture.signatureB.planeNormal[2] = nArr[2].as<float>();
        }
        gesture.signatureB.valid = true;
    }

    JsonObjectConst sigAG = obj["sig_a_gyro"];
    if (!sigAG.isNull()) {
        gesture.signatureAGyro.amplitude   = sigAG["amp"]    | 0.0f;
        gesture.signatureAGyro.peak        = sigAG["peak"]   | 0.0f;
        gesture.signatureAGyro.linearity   = sigAG["lin"]    | 0.0f;
        gesture.signatureAGyro.duration    = sigAG["dur"]    | 0.0f;
        gesture.signatureAGyro.smoothness  = sigAG["sm"]     | 0.0f;
        gesture.signatureAGyro.rotation    = sigAG["rot"]    | 0.0f;
        gesture.signatureAGyro.symmetry    = sigAG["sym"]    | 0.0f;
        JsonArrayConst nArr = sigAG["n"];
        if (!nArr.isNull() && nArr.size() >= 3) {
            gesture.signatureAGyro.planeNormal[0] = nArr[0].as<float>();
            gesture.signatureAGyro.planeNormal[1] = nArr[1].as<float>();
            gesture.signatureAGyro.planeNormal[2] = nArr[2].as<float>();
        }
        gesture.signatureAGyro.valid = true;
    }

    return true;
}

// ============================================================================
// loadCategory() — Load all gestures for a category from SPIFFS JSON file
// ============================================================================

bool GestureLoader::loadCategory(Category cat, std::vector<GestureDefinition>& gestures) {
    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    const char* path = getCategoryPath(cat);
    if (path == nullptr) {
        Serial.println("[GestureLoader] Unknown category");
        return false;
    }

    if (!SPIFFS.exists(path)) {
        Serial.printf("[GestureLoader] File not found: %s\n", path);
        return false;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open: %s\n", path);
        return false;
    }

    // FIX M12: Limite de tamanho para evitar heap exhaustion com JSON corrompido
    // Maior JSON esperado: ~40 gestos * ~300 bytes = ~12KB. Limite de 32KB e seguro.
    size_t fileSize = file.size();
    if (fileSize > 32768) {
        Serial.printf("[GestureLoader] File too large (%d bytes): %s\n", (int)fileSize, path);
        file.close();
        return false;
    }

    // ArduinoJson v7: JsonDocument unificado (aloca no heap dinamicamente)
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    gestures.clear();
    gestures.reserve(arr.size());

    for (JsonObjectConst obj : arr) {
        GestureDefinition gesture;
        if (parseGestureObj(static_cast<const void*>(&obj), gesture)) {
            gestures.push_back(gesture);
        } else {
            Serial.printf("[GestureLoader] Skipped invalid gesture in %s\n", path);
        }
    }

    Serial.printf("[GestureLoader] Loaded %d gestures from %s\n",
                  static_cast<int>(gestures.size()), path);
    return true;
}

// ============================================================================
// loadProfile() — Load all gestures for a profile from SPIFFS JSON file
// ============================================================================

bool GestureLoader::loadProfile(Profile profile, std::vector<GestureDefinition>& gestures) {
    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    const char* path = getProfilePath(profile);
    if (path == nullptr) {
        Serial.printf("[GestureLoader] Unknown profile %d\n", static_cast<int>(profile));
        return false;
    }

    if (!SPIFFS.exists(path)) {
        Serial.printf("[GestureLoader] File not found: %s\n", path);
        return false;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open: %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    gestures.clear();
    gestures.reserve(arr.size());

    for (JsonObjectConst obj : arr) {
        GestureDefinition gesture;
        if (parseGestureObj(static_cast<const void*>(&obj), gesture)) {
            gestures.push_back(gesture);
        } else {
            Serial.printf("[GestureLoader] Skipped invalid gesture in %s\n", path);
        }
    }

    Serial.printf("[GestureLoader] Loaded %d profile gestures from %s\n",
                  static_cast<int>(gestures.size()), path);
    return true;
}

// ============================================================================
// loadContexts() — Load context definitions from SPIFFS
// ============================================================================

bool GestureLoader::loadContexts(std::vector<ContextDefinition>& contexts) {
    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    const char* path = "/g/contexts.json";
    if (!SPIFFS.exists(path)) {
        Serial.printf("[GestureLoader] File not found: %s\n", path);
        return false;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open: %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    contexts.clear();
    contexts.reserve(arr.size());

    for (JsonObjectConst obj : arr) {
        ContextDefinition ctx;

        ctx.id = obj["id"] | static_cast<uint8_t>(0);
        const char* prefix = obj["prefix"] | "";
        const char* audio = obj["audio_file"] | "";

        strncpy(ctx.prefix, prefix, sizeof(ctx.prefix) - 1);
        ctx.prefix[sizeof(ctx.prefix) - 1] = '\0';

        strncpy(ctx.audioFile, audio, sizeof(ctx.audioFile) - 1);
        ctx.audioFile[sizeof(ctx.audioFile) - 1] = '\0';

        // Parse trajectory with grid bounds validation (Fix H1)
        ctx.trajectory.clear();
        JsonArrayConst traj = obj["trajectory"];
        if (!traj.isNull()) {
            for (JsonArrayConst point : traj) {
                if (point.size() >= 3) {
                    int valX = point[0].as<int>();
                    int valY = point[1].as<int>();
                    int valZ = point[2].as<int>();

                    // Validate within GRID_MIN..GRID_MAX
                    if (valX < GRID_MIN || valX > GRID_MAX ||
                        valY < GRID_MIN || valY > GRID_MAX ||
                        valZ < GRID_MIN || valZ > GRID_MAX) {
                        Serial.printf("[GestureLoader] WARNING: Context '%s' trajectory point [%d,%d,%d] "
                                      "out of range [%d..%d], skipping point\n",
                                      prefix, valX, valY, valZ, GRID_MIN, GRID_MAX);
                        continue;
                    }

                    int8_t x = static_cast<int8_t>(valX);
                    int8_t y = static_cast<int8_t>(valY);
                    int8_t z = static_cast<int8_t>(valZ);
                    ctx.trajectory.push_back(Point3D(x, y, z));
                }
            }
        }

        // FIX FLOW-07: Carregar assinatura orbital do contexto (se salva)
        JsonObjectConst sigObj = obj["sig_a"];
        if (!sigObj.isNull() && (sigObj["valid"] | false)) {
            ctx.signature.amplitude  = sigObj["amp"]  | 0.0f;
            ctx.signature.peak       = sigObj["peak"] | 0.0f;
            ctx.signature.linearity  = sigObj["lin"]  | 0.0f;
            ctx.signature.duration   = sigObj["dur"]  | 0.0f;
            ctx.signature.smoothness = sigObj["sm"]   | 0.0f;
            ctx.signature.rotation   = sigObj["rot"]  | 0.0f;
            ctx.signature.symmetry   = sigObj["sym"]  | 0.0f;
            JsonArrayConst plane = sigObj["plane"];
            if (!plane.isNull() && plane.size() >= 3) {
                ctx.signature.planeNormal[0] = plane[0].as<float>();
                ctx.signature.planeNormal[1] = plane[1].as<float>();
                ctx.signature.planeNormal[2] = plane[2].as<float>();
            }
            ctx.signature.valid = true;
        }

        contexts.push_back(ctx);
    }

    Serial.printf("[GestureLoader] Loaded %d contexts\n", static_cast<int>(contexts.size()));
    return true;
}

// ============================================================================
// loadCustom() — Load custom gestures from /data/gestures/custom.json
// ============================================================================

bool GestureLoader::loadCustom(std::vector<GestureDefinition>& gestures) {
    gestures.clear();

    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    // FIX ALT-02: Path consistente com getFilePathForGestureId()
    const char* path = "/g/custom.json";

    if (!SPIFFS.exists(path)) {
        // No custom gestures file — not an error, just empty
        Serial.println("[GestureLoader] No custom gestures file found (OK)");
        return true;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open: %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    gestures.reserve(arr.size());

    for (JsonObjectConst obj : arr) {
        GestureDefinition gesture;
        if (parseGestureObj(static_cast<const void*>(&obj), gesture)) {
            // Override ID parsing for CUxx format: high byte 0x06 (custom)
            const char* idStr = obj["id"] | "";
            if (strlen(idStr) >= 3 && idStr[0] == 'C' && idStr[1] == 'U') {
                uint8_t lowByte = static_cast<uint8_t>(atoi(idStr + 2));
                gesture.id = static_cast<uint16_t>((0x06 << 8) | lowByte);
            }
            gestures.push_back(gesture);
        } else {
            Serial.printf("[GestureLoader] Skipped invalid custom gesture in %s\n", path);
        }
    }

    Serial.printf("[GestureLoader] Loaded %d custom gestures from %s\n",
                  static_cast<int>(gestures.size()), path);
    return true;
}

// ============================================================================
// getFilePathForGestureId() — Map gesture ID prefix to SPIFFS file path
// ============================================================================

const char* GestureLoader::getFilePathForGestureId(const char* gestureId) {
    if (gestureId == nullptr || gestureId[0] == '\0') {
        return nullptr;
    }

    // Single-letter prefixes
    if (gestureId[0] == 'G')  return getCategoryPath(CAT_GERAL);

    // Two-letter prefixes
    if (strlen(gestureId) < 2) return nullptr;

    char p0 = gestureId[0];
    char p1 = gestureId[1];

    if (p0 == 'E' && p1 == 'M') return getCategoryPath(CAT_EMERGENCIA);
    if (p0 == 'C' && p1 == 'A') return getCategoryPath(CAT_CASA);
    if (p0 == 'T' && p1 == 'R') return getCategoryPath(CAT_TRABALHO);
    if (p0 == 'S' && p1 == 'O') return getCategoryPath(CAT_SOCIAL);
    if (p0 == 'H' && p1 == 'O') return getProfilePath(PROFILE_HOSPITAL);
    // FIX ALT-02: Usar paths consistentes (/g/) em vez de /data/gestures/ (longo).
    // Antes: escola e custom usavam paths longos, causando save/load em arquivos diferentes.
    if (p0 == 'E' && p1 == 'S') return getProfilePath(PROFILE_ESCOLA);
    if (p0 == 'T' && p1 == 'P') return getProfilePath(PROFILE_TRANSPORTE);
    if (p0 == 'A' && p1 == 'U') return getProfilePath(PROFILE_AUTOMACAO);
    if (p0 == 'C' && p1 == 'U') return "/g/custom.json";

    return nullptr;
}

// ============================================================================
// trajectoryToJson() — Convert trajectory vector to JSON array
// ============================================================================

void GestureLoader::trajectoryToJson(void* arrPtr, const std::vector<Point3D>& trajectory) {
    JsonArray& arr = *static_cast<JsonArray*>(arrPtr);
    for (size_t i = 0; i < trajectory.size(); i++) {
        JsonArray point = arr.add<JsonArray>();
        point.add(static_cast<int>(trajectory[i].x));
        point.add(static_cast<int>(trajectory[i].y));
        point.add(static_cast<int>(trajectory[i].z));
    }
}

// ============================================================================
// saveGestureTrajectory() — Save trained trajectory to gesture's JSON file
// ============================================================================

/**
 * Salva trajetorias treinadas de um gesto no JSON correspondente no SPIFFS.
 *
 * FIX M10: Adicionado parametro threshold (antes ignorado).
 * Apos o treino, computeAverageGesture() calcula um threshold otimo
 * baseado na distancia DTW entre as amostras. Sem salvar, o gesto
 * usava o threshold placeholder do JSON original (3.5), que pode ser
 * muito restritivo ou muito permissivo para o gesto real treinado.
 *
 * @param gestureId    ID do gesto (ex: "G01", "EM05")
 * @param trajectoryA  Trajetoria media do sensor A (3 amostras)
 * @param trajectoryB  Trajetoria media do sensor B (3 amostras)
 * @param durationMs   Duracao media do gesto em ms
 * @param threshold    Threshold DTW calculado (0 = manter o existente)
 */
bool GestureLoader::saveGestureTrajectory(const char* gestureId,
                                           const std::vector<Point3D>& trajectoryA,
                                           const std::vector<Point3D>& trajectoryB,
                                           uint16_t durationMs,
                                           float threshold,
                                           const std::vector<Point3D>& trajectoryAGyro,
                                           const std::vector<Point3D>& trajectoryBGyro,
                                           const OrbitalSignature* sigA,
                                           const OrbitalSignature* sigB,
                                           const OrbitalSignature* sigAGyro) {
    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    const char* path = getFilePathForGestureId(gestureId);
    if (path == nullptr) {
        Serial.printf("[GestureLoader] Unknown gesture prefix for ID: %s\n", gestureId);
        return false;
    }

    if (!SPIFFS.exists(path)) {
        Serial.printf("[GestureLoader] File not found: %s\n", path);
        return false;
    }

    // Read the entire JSON file
    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open for reading: %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    // Find the gesture object with matching "id" field
    bool found = false;
    for (JsonObject obj : arr) {
        const char* id = obj["id"] | "";
        if (strcmp(id, gestureId) == 0) {
            // Replace trajectory_a
            obj.remove("trajectory_a");
            JsonArray trajA = obj["trajectory_a"].to<JsonArray>();
            trajectoryToJson(static_cast<void*>(&trajA), trajectoryA);

            // Substitui trajectory_b
            obj.remove("trajectory_b");
            JsonArray trajB = obj["trajectory_b"].to<JsonArray>();
            trajectoryToJson(static_cast<void*>(&trajB), trajectoryB);

            // Atualiza duracao media
            obj["duration_ms"] = durationMs;

            // FIX M10: Salva threshold calculado (se fornecido)
            if (threshold > 0.0f) {
                obj["threshold"] = threshold;
            }

            // FIX INSTALL-20: Salvar trajetorias de gyro
            if (!trajectoryAGyro.empty()) {
                obj.remove("trajectory_a_gyro");
                JsonArray trajAG = obj["trajectory_a_gyro"].to<JsonArray>();
                trajectoryToJson(static_cast<void*>(&trajAG), trajectoryAGyro);
            }
            if (!trajectoryBGyro.empty()) {
                obj.remove("trajectory_b_gyro");
                JsonArray trajBG = obj["trajectory_b_gyro"].to<JsonArray>();
                trajectoryToJson(static_cast<void*>(&trajBG), trajectoryBGyro);
            }

            // Marca como treinado (gestos normais usam o campo trained do JSON)
            obj["trained"] = true;

            // Aviso se assinatura invalida
            if (!sigA || !sigA->valid) {
                Serial.printf("[GestureLoader] AVISO: sigA invalida para '%s' "
                              "(gesto curto? matching usara fallback sequenceSimilarity)\n", gestureId);
            }

            // === Modelo Orbital: salvar assinaturas orbitais (se fornecidas) ===
            if (sigA && sigA->valid) {
                obj.remove("sig_a");
                JsonObject sa = obj["sig_a"].to<JsonObject>();
                sa["amp"]  = sigA->amplitude;
                sa["peak"] = sigA->peak;
                sa["lin"]  = sigA->linearity;
                sa["dur"]  = sigA->duration;
                sa["sm"]   = sigA->smoothness;
                sa["rot"]  = sigA->rotation;
                sa["sym"]  = sigA->symmetry;
                JsonArray nA = sa["n"].to<JsonArray>();
                nA.add(sigA->planeNormal[0]);
                nA.add(sigA->planeNormal[1]);
                nA.add(sigA->planeNormal[2]);
            }
            if (sigB && sigB->valid) {
                obj.remove("sig_b");
                JsonObject sb = obj["sig_b"].to<JsonObject>();
                sb["amp"]  = sigB->amplitude;
                sb["peak"] = sigB->peak;
                sb["lin"]  = sigB->linearity;
                sb["dur"]  = sigB->duration;
                sb["sm"]   = sigB->smoothness;
                sb["rot"]  = sigB->rotation;
                sb["sym"]  = sigB->symmetry;
                JsonArray nB = sb["n"].to<JsonArray>();
                nB.add(sigB->planeNormal[0]);
                nB.add(sigB->planeNormal[1]);
                nB.add(sigB->planeNormal[2]);
            }
            if (sigAGyro && sigAGyro->valid) {
                obj.remove("sig_a_gyro");
                JsonObject sg = obj["sig_a_gyro"].to<JsonObject>();
                sg["amp"]  = sigAGyro->amplitude;
                sg["peak"] = sigAGyro->peak;
                sg["lin"]  = sigAGyro->linearity;
                sg["dur"]  = sigAGyro->duration;
                sg["sm"]   = sigAGyro->smoothness;
                sg["rot"]  = sigAGyro->rotation;
                sg["sym"]  = sigAGyro->symmetry;
                JsonArray nG = sg["n"].to<JsonArray>();
                nG.add(sigAGyro->planeNormal[0]);
                nG.add(sigAGyro->planeNormal[1]);
                nG.add(sigAGyro->planeNormal[2]);
            }

            found = true;
            break;
        }
    }

    if (!found) {
        Serial.printf("[GestureLoader] Gesture ID '%s' not found in %s\n", gestureId, path);
        return false;
    }

    // Escreve JSON completo de volta no SPIFFS (FILE_WRITE trunca)
    file = SPIFFS.open(path, "w");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open for writing: %s\n", path);
        return false;
    }

    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        Serial.printf("[GestureLoader] Failed to write JSON to %s\n", path);
        return false;
    }

    Serial.printf("[GestureLoader] Saved trajectory for '%s' to %s (%d bytes)\n",
                  gestureId, path, static_cast<int>(written));
    return true;
}

// ============================================================================
// resetGestureTraining() — Limpa treino de um gesto especifico
// ============================================================================

bool GestureLoader::resetGestureTraining(const char* gestureId) {
    if (!spiffsReady) return false;

    // Determina se e contexto (CX01) ou gesto normal (G01)
    bool isContext = (gestureId[0] == 'C' && gestureId[1] == 'X');

    if (isContext) {
        // Contextos estao em contexts.json
        const char* path = "/g/contexts.json";
        if (!SPIFFS.exists(path)) return false;

        File file = SPIFFS.open(path, "r");
        if (!file) return false;

        JsonDocument doc;
        if (deserializeJson(doc, file)) { file.close(); return false; }
        file.close();

        // Extrair ID numerico de "CX01" -> 1
        int ctxId = atoi(gestureId + 2);
        bool found = false;

        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            if ((obj["id"] | 0) == ctxId) {
                // Limpar trajectoria para placeholder central
                obj.remove("trajectory");
                JsonArray traj = obj["trajectory"].to<JsonArray>();
                JsonArray pt = traj.add<JsonArray>();
                pt.add(99); pt.add(99); pt.add(99);  // Fora da grid — impossivel match

                // Remover assinatura orbital
                obj.remove("sig");
                obj.remove("trained");
                found = true;
                break;
            }
        }

        if (!found) return false;

        file = SPIFFS.open(path, "w");
        if (!file) return false;
        serializeJson(doc, file);
        file.close();
    } else {
        // Gestos normais — encontrar o arquivo correto
        const char* path = getFilePathForGestureId(gestureId);
        if (!path || !SPIFFS.exists(path)) return false;

        File file = SPIFFS.open(path, "r");
        if (!file) return false;

        JsonDocument doc;
        if (deserializeJson(doc, file)) { file.close(); return false; }
        file.close();

        bool found = false;
        JsonArray arr = doc.as<JsonArray>();
        for (JsonObject obj : arr) {
            const char* id = obj["id"] | "";
            if (strcmp(id, gestureId) == 0) {
                // Resetar trajectorias para placeholder impossivel (fora da grid)
                obj.remove("trajectory_a");
                JsonArray trajA = obj["trajectory_a"].to<JsonArray>();
                JsonArray ptA = trajA.add<JsonArray>();
                ptA.add(99); ptA.add(99); ptA.add(99);

                obj.remove("trajectory_b");
                JsonArray trajB = obj["trajectory_b"].to<JsonArray>();
                JsonArray ptB = trajB.add<JsonArray>();
                ptB.add(99); ptB.add(99); ptB.add(99);

                // Limpar gyro e assinaturas
                obj.remove("trajectory_a_gyro");
                obj.remove("trajectory_b_gyro");
                obj.remove("sig_a");
                obj.remove("sig_b");
                obj.remove("sig_a_gyro");

                obj["trained"] = false;
                obj["sample_count"] = 0;
                found = true;
                break;
            }
        }

        if (!found) return false;

        file = SPIFFS.open(path, "w");
        if (!file) return false;
        serializeJson(doc, file);
        file.close();
    }

    Serial.printf("[GestureLoader] Training reset for '%s'\n", gestureId);
    return true;
}

// ============================================================================
// saveContextTrajectory() — Save trained context trajectory to contexts.json
// ============================================================================

bool GestureLoader::saveContextTrajectory(const char* contextId,
                                           const std::vector<Point3D>& trajectory,
                                           uint16_t durationMs,
                                           const OrbitalSignature* sig) {
    if (!spiffsReady) {
        Serial.println("[GestureLoader] SPIFFS not initialized");
        return false;
    }

    const char* path = "/g/contexts.json";

    if (!SPIFFS.exists(path)) {
        Serial.printf("[GestureLoader] File not found: %s\n", path);
        return false;
    }

    // Read the entire JSON file
    File file = SPIFFS.open(path, "r");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open for reading: %s\n", path);
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        Serial.printf("[GestureLoader] JSON parse error in %s: %s\n", path, error.c_str());
        return false;
    }

    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) {
        Serial.printf("[GestureLoader] Root is not array in %s\n", path);
        return false;
    }

    // Find the context object with matching "id" field
    bool found = false;
    int targetId = atoi(contextId);
    for (JsonObject obj : arr) {
        int id = obj["id"] | -1;
        if (id == targetId) {
            // Replace trajectory
            obj.remove("trajectory");
            JsonArray traj = obj["trajectory"].to<JsonArray>();
            trajectoryToJson(static_cast<void*>(&traj), trajectory);

            // Update duration_ms
            obj["duration_ms"] = durationMs;

            // Salvar assinatura orbital e marcar como treinado SOMENTE se valida.
            // Antes: salvava trained=true sem sig_a → JSON inconsistente → gesto
            // "treinado" que nunca funcionava (signature.valid=false no reload).
            if (sig != nullptr && sig->valid) {
                obj["trained"] = true;
                obj.remove("sig_a");
                JsonObject sigObj = obj["sig_a"].to<JsonObject>();
                sigObj["amp"] = sig->amplitude;
                sigObj["peak"] = sig->peak;
                sigObj["lin"] = sig->linearity;
                sigObj["dur"] = sig->duration;
                sigObj["sm"] = sig->smoothness;
                sigObj["rot"] = sig->rotation;
                sigObj["sym"] = sig->symmetry;
                // Plano orbital
                JsonArray plane = sigObj["plane"].to<JsonArray>();
                plane.add(sig->planeNormal[0]);
                plane.add(sig->planeNormal[1]);
                plane.add(sig->planeNormal[2]);
                sigObj["valid"] = true;
            } else {
                // Assinatura invalida — gesto muito curto/rapido
                obj["trained"] = false;
                Serial.printf("[GestureLoader] ERRO: assinatura invalida para contexto '%s' "
                              "(gesto muito curto, precisa movimento mais amplo/lento)\n", contextId);
                // Salvar JSON atualizado (com trained=false) e retornar ERRO
                file = SPIFFS.open(path, "w");
                if (file) { serializeJson(doc, file); file.close(); }
                return false;
            }

            found = true;
            break;
        }
    }

    if (!found) {
        Serial.printf("[GestureLoader] Context ID '%s' not found in %s\n", contextId, path);
        return false;
    }

    // Write back to SPIFFS
    file = SPIFFS.open(path, "w");
    if (!file) {
        Serial.printf("[GestureLoader] Failed to open for writing: %s\n", path);
        return false;
    }

    size_t written = serializeJson(doc, file);
    file.close();

    if (written == 0) {
        Serial.printf("[GestureLoader] Failed to write JSON to %s\n", path);
        return false;
    }

    Serial.printf("[GestureLoader] Saved context trajectory for '%s' to %s (%d bytes)\n",
                  contextId, path, static_cast<int>(written));
    return true;
}

// ============================================================================
// isGestureTrained() — Check if a gesture has real trained trajectory data
// ============================================================================

bool GestureLoader::isGestureTrained(const char* gestureId) {
    if (!spiffsReady) {
        return false;
    }

    const char* path = getFilePathForGestureId(gestureId);
    if (path == nullptr) {
        return false;
    }

    if (!SPIFFS.exists(path)) {
        return false;
    }

    File file = SPIFFS.open(path, "r");
    if (!file) {
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, file);
    file.close();

    if (error) {
        return false;
    }

    JsonArrayConst arr = doc.as<JsonArrayConst>();
    if (arr.isNull()) {
        return false;
    }

    for (JsonObjectConst obj : arr) {
        const char* id = obj["id"] | "";
        if (strcmp(id, gestureId) == 0) {
            return obj["trained"] | false;
        }
    }

    return false;
}

// ============================================================================
// getTotalGestureCount() — Count gestures across all categories
// ============================================================================

int GestureLoader::getTotalGestureCount() {
    if (!spiffsReady) {
        return -1;
    }

    int total = 0;
    Category categories[] = { CAT_GERAL, CAT_EMERGENCIA, CAT_CASA, CAT_TRABALHO, CAT_SOCIAL };

    for (int i = 0; i < 5; i++) {
        const char* path = getCategoryPath(categories[i]);
        if (path == nullptr) continue;

        if (!SPIFFS.exists(path)) continue;

        File file = SPIFFS.open(path, "r");
        if (!file) continue;

        JsonDocument doc;
        DeserializationError error = deserializeJson(doc, file);
        file.close();

        if (error) continue;

        JsonArrayConst arr = doc.as<JsonArrayConst>();
        if (!arr.isNull()) {
            total += static_cast<int>(arr.size());
        }
    }

    return total;
}
