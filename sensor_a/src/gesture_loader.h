/**
 * GESTUUM — Gesture Loader Header
 * Bloco: 5.1 — Gesture JSON Database + GestureLoader
 * Responsibility: Declare GestureLoader class that loads gesture definitions
 *                 from JSON files stored on SPIFFS.
 */

#ifndef GESTUUM_GESTURE_LOADER_H
#define GESTUUM_GESTURE_LOADER_H

#include "../../shared/src/constants.h"
#include "../../shared/src/protocol.h"
#include "../../shared/src/gesture_data.h"
#include <vector>

/**
 * Loads gesture definitions from JSON files on SPIFFS.
 *
 * Each category has its own JSON file under /data/gestures/:
 *   /data/gestures/geral.json
 *   /data/gestures/emergencia.json
 *   /data/gestures/casa.json
 *   /data/gestures/trabalho.json
 *   /data/gestures/social.json
 *
 * Profile files are also under /data/gestures/:
 *   /data/gestures/hospital.json
 *   /data/gestures/escola.json
 *   /data/gestures/terapia.json
 *   /data/gestures/restaurante.json
 *   /data/gestures/transporte.json
 *   /data/gestures/lazer.json
 *
 * JSON format per gesture:
 *   {
 *     "id": "G01",
 *     "name": "agua",
 *     "category": "GERAL",
 *     "audio_file": "agua.wav",
 *     "threshold": 3.5,
 *     "is_solo": false,
 *     "automation_cmd": 0,
 *     "trajectory_a": [[5,5,5],[5,6,5],...],
 *     "trajectory_b": [[5,5,5],[5,5,6],...],
 *     "duration_ms": 1200
 *   }
 */
class GestureLoader {
public:
    GestureLoader();

    /**
     * Initialize SPIFFS filesystem.
     * Must be called once before any load operations.
     * @return true if SPIFFS mounted successfully.
     */
    bool begin();

    /**
     * Load all gesture definitions for a given category from its JSON file.
     * Clears the provided vector before populating it.
     * @param cat Category to load (CAT_GERAL, CAT_EMERGENCIA, etc.)
     * @param gestures Output vector to populate with gesture definitions.
     * @return true if file was read and parsed successfully.
     */
    bool loadCategory(Category cat, std::vector<GestureDefinition>& gestures);

    /**
     * Load all gesture definitions for a given profile from its JSON file.
     * Clears the provided vector before populating it.
     * @param profile Profile to load (PROFILE_HOSPITAL, PROFILE_ESCOLA, etc.)
     * @param gestures Output vector to populate with gesture definitions.
     * @return true if file was read and parsed successfully.
     */
    bool loadProfile(Profile profile, std::vector<GestureDefinition>& gestures);

    /**
     * Load context definitions from contexts.json on SPIFFS.
     * @param contexts Output vector to populate with context definitions.
     * @return true if file was read and parsed successfully.
     */
    bool loadContexts(std::vector<ContextDefinition>& contexts);

    /**
     * Load all custom gesture definitions from /data/gestures/custom.json.
     * Clears the provided vector before populating it.
     * @param gestures Output vector to populate with gesture definitions.
     * @return true if file was read and parsed successfully (or file doesn't exist — returns true with empty vector).
     */
    bool loadCustom(std::vector<GestureDefinition>& gestures);

    /**
     * Save a trained gesture's trajectories to its category JSON file on SPIFFS.
     * Finds the gesture by ID prefix, reads the full JSON, updates trajectory
     * fields, writes it back.
     * @param gestureId String ID (e.g., "G01", "EM03")
     * @param trajectoryA New reference trajectory for sensor A
     * @param trajectoryB New reference trajectory for sensor B
     * @param durationMs Gesture duration in milliseconds
     * @return true on success
     */
    // FIX M10: Adicionado parametro threshold (default 0 = manter existente)
    // FIX INSTALL-20: Adicionado trajetorias de gyro
    bool saveGestureTrajectory(const char* gestureId,
                               const std::vector<Point3D>& trajectoryA,
                               const std::vector<Point3D>& trajectoryB,
                               uint16_t durationMs,
                               float threshold,
                               const std::vector<Point3D>& trajectoryAGyro,
                               const std::vector<Point3D>& trajectoryBGyro,
                               const OrbitalSignature* sigA = nullptr,
                               const OrbitalSignature* sigB = nullptr,
                               const OrbitalSignature* sigAGyro = nullptr);

    /**
     * Reset training for a specific gesture — marca trained=false,
     * limpa trajectórias e assinaturas orbitais no JSON.
     * @param gestureId String ID (e.g., "G01", "CX01")
     * @return true on success
     */
    bool resetGestureTraining(const char* gestureId);

    /**
     * Save a trained context trajectory to contexts.json on SPIFFS.
     * @param contextId String context ID (numeric, e.g., "1", "5")
     * @param trajectory New reference trajectory
     * @param durationMs Context gesture duration in milliseconds
     * @return true on success
     */
    bool saveContextTrajectory(const char* contextId,
                               const std::vector<Point3D>& trajectory,
                               uint16_t durationMs,
                               const OrbitalSignature* sig = nullptr);

    /**
     * Check if a gesture has been trained (has real trajectory data).
     * Reads the JSON file and checks for "trained": true field.
     * @param gestureId String ID (e.g., "G01", "EM03")
     * @return true if the gesture has the "trained" flag set to true
     */
    bool isGestureTrained(const char* gestureId);

    /**
     * Get total number of gestures across all category files on SPIFFS.
     * Opens and counts entries in each category file.
     * @return Total gesture count, or -1 on error.
     */
    int getTotalGestureCount();

    /**
     * Get the SPIFFS file path for a given profile.
     * @param profile Profile enum value.
     * @return Null-terminated path string, or nullptr for unknown/base profile.
     */
    const char* getProfilePath(Profile profile);

private:
    bool spiffsReady;

    /**
     * Parse a single gesture JSON object into a GestureDefinition.
     * @param obj ArduinoJson JsonObject for the gesture.
     * @param gesture Output GestureDefinition to populate.
     * @return true if parsing succeeded.
     */
    bool parseGestureObj(const void* obj, GestureDefinition& gesture);

    /**
     * Determine the SPIFFS file path for a gesture based on its string ID prefix.
     * @param gestureId String ID (e.g., "G01", "EM03", "HO02")
     * @return File path, or nullptr if prefix is unknown.
     */
    const char* getFilePathForGestureId(const char* gestureId);

    /**
     * Convert a trajectory vector to a JSON array: [[x,y,z], [x,y,z], ...]
     * @param arrPtr Pointer to JsonArray (void* to avoid ArduinoJson in header)
     * @param trajectory Source trajectory
     */
    void trajectoryToJson(void* arrPtr, const std::vector<Point3D>& trajectory);

    /**
     * Get the SPIFFS file path for a given category.
     * @param cat Category enum value.
     * @return Null-terminated path string, or nullptr for unknown category.
     */
    const char* getCategoryPath(Category cat);

    /**
     * Parse a string gesture ID (e.g., "G01", "EM02") into a numeric uint16_t.
     * Format: category prefix encodes high byte, number encodes low byte.
     * @param idStr String ID from JSON.
     * @return Numeric gesture ID.
     */
    uint16_t parseGestureId(const char* idStr);

    /**
     * Parse a category string (e.g., "GERAL") into a Category enum.
     * @param catStr Category string from JSON.
     * @return Category enum value (defaults to CAT_GERAL if unknown).
     */
    Category parseCategoryStr(const char* catStr);
};

#endif // GESTUUM_GESTURE_LOADER_H
