/**
 * GESTUUM — Central Configuration Command Handler
 * Responsibility: Receive JSON commands from BLE or Serial and execute them.
 *
 * Supported commands:
 *   get_config           — Return current device configuration
 *   set_voice            — Change active voice (homem/mulher/menino/menina)
 *   set_profile          — Activate or deactivate a profile
 *   set_name             — Change BLE device name (persisted in NVS)
 *   test_audio           — Play an audio file for testing
 *   upload_start         — Begin audio file upload to SPIFFS
 *   upload_chunk         — Send a base64-encoded data chunk
 *   upload_end           — Finalize audio file upload
 *   list_gestures        — List all loaded gestures
 *   get_profiles         — Get profile activation status
 *   add_custom_gesture   — Create a custom gesture entry in custom.json
 *   remove_custom_gesture— Remove a custom gesture by name from custom.json
 *   list_custom_gestures — List only custom gestures from custom.json
 *   set_level            — Set gesture capture level (0=limited, 1=standard, 2=advanced)
 *   reboot               — Restart the device
 *   set_silent_errors    — Enable/disable silent error mode (UX12)
 *   train_start          — Begin gesture training for a given gesture_id
 *   train_cancel         — Cancel active training session
 *   train_status         — Query training progress
 *   train_sample_done    — Notify that a training sample was captured (internal)
 *   train_save           — Compute average and save trained gesture
 *   list_trainable       — List all gestures with trained/untrained status
 */

#ifndef CONFIG_HANDLER_H
#define CONFIG_HANDLER_H

#include <Arduino.h>
#include <FS.h>

// Forward declarations
class VoiceManager;
class GestureEngine;
class AudioPlayer;
class BTAudio;

// FIX ALT-02: Path consistente com gesture_loader (/g/ em vez de /data/gestures/)
#define CUSTOM_GESTURES_PATH "/g/custom.json"

class ConfigHandler {
public:
    void begin(VoiceManager* vm, GestureEngine* ge, AudioPlayer* ap, BTAudio* bt = nullptr);
    void handleCommand(const char* json);

    // Set response callback (routes to BLE or Serial depending on source)
    typedef void (*ResponseCallback)(const char* json);
    void setResponseCallback(ResponseCallback cb);

    // Training start callback — called when train_start command is received.
    // Main.cpp sets this to enter STATE_TRAINING on the state machine.
    // Parameters: (gestureId, gestureName)
    typedef void (*TrainingStartCallback)(const char* gestureId, const char* gestureName);
    void setTrainingStartCallback(TrainingStartCallback cb);

    // Training cancel callback — called when train_cancel command is received.
    typedef void (*TrainingCancelCallback)();
    void setTrainingCancelCallback(TrainingCancelCallback cb);

    // Training state query
    bool isTrainingActive() const;

    // Reset training state (called by main.cpp when training is cancelled via UI)
    void resetTrainingState();

    // Called by main.cpp after a training sample is captured
    void notifyTrainingSampleCaptured();

    // Called by main.cpp when training completes (3 samples collected)
    void notifyTrainingComplete();

    // FIX BLE-HIGH: Abortar upload em andamento (chamado ao sair de config mode
    // ou quando BLE desconecta — evita file descriptor leak no SPIFFS)
    void abortUploadIfActive();

private:
    VoiceManager* _voiceManager;
    GestureEngine* _gestureEngine;
    AudioPlayer* _audioPlayer;
    BTAudio* _btAudio;
    ResponseCallback _responseCb;

    // Command handlers
    void cmdGetConfig();
    void cmdSetVoice(int voice);
    void cmdSetProfile(int profile, bool active);
    void cmdSetName(const char* name);
    void cmdTestAudio(const char* filename);
    void cmdUploadStart(const char* filename, int size);
    void cmdUploadChunk(int offset, const char* base64data);
    void cmdUploadEnd(const char* filename);
    void cmdListGestures();
    void cmdGetProfiles();
    void cmdAddCustomGesture(const char* name, const char* audioFile,
                             const char* category, bool isSolo, int automationCmd);
    void cmdRemoveCustomGesture(const char* name);
    void cmdListCustomGestures();
    void cmdSetLevel(int level);
    void cmdSetSilentErrors(bool value);
    void cmdReboot();
    void cmdFactoryReset();
    void cmdBtDiscover();
    void cmdBtStatus();

    // Training command handlers
    void cmdTrainStart(const char* gestureId);
    void cmdTrainCancel();
    void cmdTrainStatus();
    void cmdTrainSampleDone();
    void cmdTrainSave();
    void cmdListTrainableGestures();

    // Upload state
    bool _uploading;
    char _uploadFilename[64];
    int _uploadSize;
    int _uploadReceived;
    File _uploadFile;

    // Training state
    bool _trainingActive;
    char _trainingGestureId[16];
    char _trainingGestureName[32];

    // Training callbacks to main.cpp state machine
    TrainingStartCallback _trainingStartCb;
    TrainingCancelCallback _trainingCancelCb;

    // Custom gesture helpers
    /**
     * Generate the next custom gesture ID (CU01, CU02, ...).
     * Reads existing custom.json to find the highest CUxx number.
     * @param outId Output buffer (at least 8 chars)
     * @param maxLen Size of outId buffer
     * @return true if ID generated successfully
     */
    bool generateCustomId(char* outId, size_t maxLen);

    /**
     * Reload custom gestures into the gesture engine.
     * Loads custom.json and appends gestures to the engine's current set.
     */
    void reloadCustomGestures();

    void sendResponse(const char* json);
};

extern ConfigHandler configHandler;

#endif // CONFIG_HANDLER_H
