/**
 * GESTUUM — Audio Player (HAT-SPK2 / MAX98357A)
 * Bloco: 3.3 — Sensor A - Audio Player
 * Responsibility: WAV playback from SPIFFS via M5.Speaker (M5Unified).
 *
 * FIX INSTALL-13: Migrado de ESP32-audioI2S para M5.Speaker.
 * M5Unified gerencia o HAT-SPK2 (I2S_NUM_1) via cfg.external_speaker.hat_spk2.
 * Nao ha mais conflito de driver I2S.
 */

#ifndef GESTUUM_AUDIO_PLAYER_H
#define GESTUUM_AUDIO_PLAYER_H

#include <stdint.h>

// Forward declaration — evita incluir bt_audio.h em todo arquivo
class BTAudio;

/**
 * Audio player non-blocking que toca WAVs do SPIFFS via M5.Speaker
 * e opcionalmente via Bluetooth A2DP (caixa de som externa).
 * Quando BT conectado, audio sai nos DOIS (HAT-SPK2 + BT).
 *
 * Uso:
 *   AudioPlayer player;
 *   player.begin();               // chamar uma vez no setup()
 *   player.setVoicePath("/a/h/");
 *   player.playFile("agua.wav");  // toca /a/h/agua.wav
 *   // no loop():
 *   player.update();              // DEVE ser chamado a cada iteracao
 */
class AudioPlayer {
public:
    AudioPlayer();
    ~AudioPlayer() = default;

    /**
     * Inicializa SPIFFS. M5.Speaker ja esta configurado pelo M5.begin().
     * @param bt Ponteiro para BTAudio (opcional). Se fornecido, audio
     *           tambem e enviado para caixa de som BT quando conectada.
     * @return true se SPIFFS montou com sucesso.
     */
    bool begin(BTAudio* bt = nullptr);

    /**
     * Deve ser chamado a cada iteracao do loop() para processar:
     * - Deteccao de fim de audio (para sequencia de frases)
     * - Maquina de estados do beep de erro (non-blocking)
     */
    void update();

    /**
     * Toca um arquivo WAV do SPIFFS.
     * O voice path e prepended automaticamente ao filename.
     * Se path comeca com '/', trata como caminho absoluto.
     * @param path  Filename ou caminho SPIFFS, ex: "agua.wav" ou "/a/h/agua.wav".
     * @return true se o arquivo existe e o playback iniciou.
     */
    bool playFile(const char* path);

    /** Para o playback imediatamente. */
    void stop();

    /**
     * Toca dois arquivos em sequencia com gap de ~200ms.
     * Usado para frase composta: contexto + objeto (ex: "Quero" + "agua").
     * @param file1 Primeiro arquivo (toca imediatamente).
     * @param file2 Segundo arquivo (toca apos file1 terminar + gap).
     * @return true se file1 comecou a tocar.
     */
    bool playSequence(const char* file1, const char* file2);

    /** @return true enquanto audio esta tocando ou sequencia pendente. */
    bool isPlaying() const;

    /**
     * Define volume do speaker.
     * @param vol  Nivel 0-255 (passado direto para M5.Speaker).
     */
    void setVolume(uint8_t vol);

    /** @return Volume atual (0-255). */
    uint8_t getVolume() const;

    /**
     * Define prefixo do path de voz para arquivos de audio.
     * @param path  Prefixo, ex: "/a/h/". Deve terminar com '/'.
     */
    void setVoicePath(const char* path);

    // --- Beeps de feedback ------------------------------------------------

    /** Beep curto de confirmacao (1000 Hz, 100 ms). */
    void beepConfirm();

    /** Beep duplo de erro (500 Hz, 100 ms + pausa + 100 ms). Non-blocking. */
    void beepError();

    /** Beep longo de emergencia (800 Hz, 500 ms). */
    void beepEmergency();

    /** SOS em codigo Morse: ... --- ... (bips agudos em volume alto).
     *  Bloqueante (~3.6s). Usar apenas em STATE_EMERGENCY. */
    void playSOS();

    /**
     * Fix UX12: Quando ativo, beepError() e silenciado.
     * Para criancas autistas sensiveis a sons de erro.
     * @param silent  true para suprimir beeps de erro.
     */
    void setSilentErrors(bool silent);

    /**
     * Sprint C3d (Caminho C, 2026-05-02): Modo silencioso global.
     * Quando true, todos os play/beep viram no-op (early return).
     * Persistido em NVS via main.cpp setup() — aqui so guarda flag.
     */
    void setSilentAll(bool silent);
    bool isSilentAll() const { return _silentAll; }

private:
    bool    _initialized;
    bool    _playing;       // true enquanto um WAV esta tocando
    uint8_t _volume;
    char    _voicePath[32]; // Prefixo do path de voz (ex: "/a/h/")
    BTAudio* _btAudio;      // Ponteiro para saida BT (nullptr se nao usar)

    // FIX AUDIO-02: Reduzido para 150 (~60%) para compensar ganho analogico
    // de +9dB do MAX98357 (pino GAIN flutuante). Speaker de 1W satura com
    // valores maiores. BT nao e afetado (volume controlado pela caixa).
    static constexpr uint8_t VOLUME_DEFAULT = 217;
    static constexpr uint8_t VOLUME_MAX     = 255;

    // Fila de sequencia para frase composta (contexto + objeto)
    char    _queuedFile[64];             // Segundo arquivo pendente
    bool    _sequencePending;            // true quando tem arquivo na fila
    unsigned long _sequenceGapStartMs;   // Timestamp do inicio do gap
    static constexpr unsigned long SEQUENCE_GAP_MS = 200;  // Gap entre arquivos

    // Maquina de estados do beep de erro (non-blocking)
    enum BeepState : uint8_t {
        BEEP_IDLE,
        BEEP_ERROR_TONE1,
        BEEP_ERROR_PAUSE,
        BEEP_ERROR_TONE2
    };

    BeepState    _beepState;
    unsigned long _beepStartMs;
    bool         _silentErrors;  // Fix UX12: suprimir beeps de erro
    bool         _silentAll = false;  // Sprint C3d: silencio global (NVS)

    bool initSPIFFS();
};

#endif // GESTUUM_AUDIO_PLAYER_H
