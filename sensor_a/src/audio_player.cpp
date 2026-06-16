/**
 * GESTUUM — Audio Player Implementation
 * Bloco: 3.3 — Sensor A - Audio Player
 * Responsibility: WAV playback from SPIFFS via M5.Speaker (M5Unified).
 *
 * FIX INSTALL-13: Migrado de ESP32-audioI2S para M5.Speaker.
 * O M5Unified gerencia o HAT-SPK2 no I2S_NUM_1 via cfg.external_speaker.hat_spk2.
 * playFile() le o WAV inteiro na PSRAM, pula o header de 44 bytes,
 * e envia os samples PCM via M5.Speaker.playRaw().
 * Beeps usam M5.Speaker.tone() que ja e non-blocking.
 * Sequencia (frase composta) detecta fim via !M5.Speaker.isPlaying() no update().
 */

#include "audio_player.h"
#include "bt_audio.h"

#include <M5Unified.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <cstring>

#include "constants.h"  // AUDIO_SAMPLE_RATE (48000)

static const char* TAG = "AudioPlayer";

// ---------------------------------------------------------------------------
// Construtor
// ---------------------------------------------------------------------------

AudioPlayer::AudioPlayer()
    : _initialized(false)
    , _playing(false)
    , _volume(VOLUME_DEFAULT)
    , _sequencePending(false)
    , _sequenceGapStartMs(0)
    , _beepState(BEEP_IDLE)
    , _beepStartMs(0)
    , _silentErrors(false)
    , _btAudio(nullptr)
{
    strncpy(_voicePath, "/a/h/", sizeof(_voicePath) - 1);
    _voicePath[sizeof(_voicePath) - 1] = '\0';
    _queuedFile[0] = '\0';
}

// ---------------------------------------------------------------------------
// API publica
// ---------------------------------------------------------------------------

bool AudioPlayer::begin(BTAudio* bt) {
    if (_initialized) {
        log_w("%s: already initialised", TAG);
        return true;
    }

    _btAudio = bt;

    if (!initSPIFFS()) {
        return false;
    }

    // M5.Speaker ja esta configurado pelo M5.begin() com hat_spk2=true.
    // Apenas garantir volume no maximo.
    M5.Speaker.setVolume(_volume);

    // Definir voice path padrao
    strncpy(_voicePath, "/a/h/", sizeof(_voicePath) - 1);
    _voicePath[sizeof(_voicePath) - 1] = '\0';

    _initialized = true;
    log_i("%s: initialised OK (volume=%u)", TAG, _volume);
    return true;
}

bool AudioPlayer::playFile(const char* path) {
    if (!_initialized) {
        log_e("%s: not initialised — call begin() first", TAG);
        return false;
    }

    // Sprint C3d: modo silencioso global suprime tudo. Mantemos o early
    // return apos a checagem de path/init pra logar mau-uso (path nulo).
    if (_silentAll) {
        return false;
    }

    if (path == nullptr || path[0] == '\0') {
        log_e("%s: playFile called with null/empty path", TAG);
        return false;
    }

    // Montar path completo: se comeca com '/' usa direto, senao prepend voice path
    char fullPath[64];
    if (path[0] == '/') {
        strncpy(fullPath, path, sizeof(fullPath) - 1);
        fullPath[sizeof(fullPath) - 1] = '\0';
    } else {
        snprintf(fullPath, sizeof(fullPath), "%s%s", _voicePath, path);
    }

    // Verificar existencia antes de tentar tocar
    if (!SPIFFS.exists(fullPath)) {
        log_e("%s: file not found: %s", TAG, fullPath);
        return false;
    }

    // Abrir arquivo WAV do SPIFFS
    File f = SPIFFS.open(fullPath, "r");
    if (!f) {
        log_e("%s: failed to open %s", TAG, fullPath);
        return false;
    }

    size_t fileSize = f.size();
    if (fileSize <= 44) {
        log_e("%s: file too small: %s (%d bytes)", TAG, fullPath, fileSize);
        f.close();
        return false;
    }

    // Pular header WAV (44 bytes) — dados PCM comecam apos o header
    f.seek(44);
    size_t dataSize = fileSize - 44;

    // Alocar buffer na PSRAM (WAVs ~20-60KB, PSRAM tem 8MB)
    int16_t* wavData = (int16_t*)ps_malloc(dataSize);
    if (wavData == nullptr) {
        // Fallback para heap normal se PSRAM nao disponivel
        wavData = (int16_t*)malloc(dataSize);
    }
    if (wavData == nullptr) {
        log_e("%s: failed to allocate %d bytes for %s", TAG, dataSize, fullPath);
        f.close();
        return false;
    }

    size_t bytesRead = f.read((uint8_t*)wavData, dataSize);
    f.close();

    if (bytesRead != dataSize) {
        log_e("%s: read mismatch: expected %d, got %d", TAG, dataSize, bytesRead);
        free(wavData);
        return false;
    }

    // Audio gerado pelo Google TTS com perfil small-bluetooth-speaker-class-device
    // a 48kHz (match exato com saida do M5.Speaker). Sem processamento no firmware.
    size_t numSamples = dataSize / 2;

    // Parar som anterior se estiver tocando
    M5.Speaker.stop();

    // FIX AUDIO-03: Desligar backlight DURANTE a copia para DMA.
    // O PWM do backlight (GPIO27, 10-12kHz) contamina o I2S.
    // Desliga antes do playRaw, restaura logo depois (o DMA continua sozinho).
    M5.Display.setBrightness(0);

    // Enviar audio para caixa BT se conectada (antes de liberar o buffer)
    if (_btAudio != nullptr && _btAudio->isConnected()) {
        _btAudio->playPCM(wavData, numSamples, AUDIO_SAMPLE_RATE);
    }

    // Tocar via M5.Speaker (HAT-SPK2) — sempre toca, mesmo com BT
    bool ok = M5.Speaker.playRaw(wavData, numSamples, AUDIO_SAMPLE_RATE, false, 1, -1, true);

    // Restaurar backlight imediatamente apos enfileirar no DMA.
    // O audio continua tocando via DMA autonomamente.
    M5.Display.setBrightness(64);

    // Liberar buffer apos enfileirar (M5.Speaker copia internamente)
    free(wavData);

    if (!ok) {
        log_e("%s: M5.Speaker.playRaw failed for %s", TAG, fullPath);
        return false;
    }

    _playing = true;
    log_i("%s: playing %s (%d bytes, %dHz)", TAG, fullPath, dataSize, AUDIO_SAMPLE_RATE);
    return true;
}

void AudioPlayer::stop() {
    M5.Speaker.stop();
    _playing = false;
    // Limpar fila de sequencia
    _queuedFile[0] = '\0';
    _sequencePending = false;
    _sequenceGapStartMs = 0;
}

bool AudioPlayer::isPlaying() const {
    // Sequencia de arquivos pendente conta como "tocando"
    if (_sequencePending) {
        return true;
    }
    // Estado real do M5.Speaker
    if (_initialized) {
        return M5.Speaker.isPlaying();
    }
    return false;
}

void AudioPlayer::setVolume(uint8_t vol) {
    _volume = vol;
    M5.Speaker.setVolume(_volume);
}

uint8_t AudioPlayer::getVolume() const {
    return _volume;
}

void AudioPlayer::setVoicePath(const char* path) {
    if (path == nullptr) {
        return;
    }
    strncpy(_voicePath, path, sizeof(_voicePath) - 1);
    _voicePath[sizeof(_voicePath) - 1] = '\0';

    // Garantir trailing slash — sem ela, path fica /a/hagua.wav
    size_t len = strlen(_voicePath);
    if (len > 0 && len < sizeof(_voicePath) - 1 && _voicePath[len - 1] != '/') {
        _voicePath[len] = '/';
        _voicePath[len + 1] = '\0';
    }

    log_i("%s: voice path set to %s", TAG, _voicePath);
}

// ---------------------------------------------------------------------------
// Sequencia de frases (contexto + objeto)
// ---------------------------------------------------------------------------

bool AudioPlayer::playSequence(const char* file1, const char* file2) {
    // Sprint C3d: modo silencioso global
    if (_silentAll) {
        return false;
    }
    if (file2 != nullptr && file2[0] != '\0') {
        // Guardar segundo arquivo para tocar apos file1 terminar
        strncpy(_queuedFile, file2, sizeof(_queuedFile) - 1);
        _queuedFile[sizeof(_queuedFile) - 1] = '\0';
        _sequencePending = true;
        _sequenceGapStartMs = 0;
    } else {
        _queuedFile[0] = '\0';
        _sequencePending = false;
    }

    // Toca file1 — update() detecta quando termina e toca file2
    bool ok = playFile(file1);

    // Se file1 falhou (arquivo nao encontrado), limpar sequencia.
    // Sem isso, _sequencePending ficaria true e isPlaying() retornaria
    // true para sempre, travando o sistema em STATE_SPEAKING.
    if (!ok) {
        _queuedFile[0] = '\0';
        _sequencePending = false;
        _sequenceGapStartMs = 0;
    }

    return ok;
}

// ---------------------------------------------------------------------------
// Beeps de feedback
// ---------------------------------------------------------------------------

void AudioPlayer::beepConfirm() {
    // Sprint C3d: modo silencioso global
    if (_silentAll) return;
    // Beep curto de confirmacao: 1000 Hz, 100 ms
    M5.Speaker.tone(1000, 100);
}

void AudioPlayer::beepError() {
    // Sprint C3d: modo silencioso global. Antecede o silentErrors (UX12).
    if (_silentAll) return;
    // Fix UX12: Pular beep quando modo silencioso ativo
    if (_silentErrors) {
        return;
    }
    // Iniciar sequencia de beep duplo de erro (non-blocking)
    // Primeiro tom: 500 Hz, 100 ms
    M5.Speaker.tone(500, 100);
    _beepState = BEEP_ERROR_TONE1;
    _beepStartMs = millis();
}

void AudioPlayer::setSilentErrors(bool silent) {
    _silentErrors = silent;
    log_i("%s: silent errors %s", TAG, silent ? "enabled" : "disabled");
}

void AudioPlayer::setSilentAll(bool silent) {
    _silentAll = silent;
    log_i("%s: SILENT ALL %s", TAG, silent ? "enabled" : "disabled");
    if (silent) {
        // Para playback em curso e limpa fila pendente
        stop();
    }
}

void AudioPlayer::beepEmergency() {
    // Sprint C3d: emergencia AINDA TOCA mesmo com silencioso global.
    // Decisao: emergencia e prioridade de seguranca, nao deve ser silenciada
    // por preferencia de UX. SOS hold no Sensor B continua tocando.
    // (Pra silenciar emergencia tambem, seria preciso opcao separada na UX.)
    M5.Speaker.tone(800, 500);
}

void AudioPlayer::playSOS() {
    // Alerta de emergencia: bip agudo repetitivo para chamar atencao.
    // Non-blocking — M5.Speaker enfileira os tons.
    // Ciclo: bip 500ms + silencio 300ms = ~800ms por bip, 3 bips = ~2.4s
    static constexpr int FREQ = 2000;    // Frequencia alta para chamar atencao
    static constexpr int BIP = 500;      // Duracao do bip
    static constexpr int PAUSA = 300;    // Pausa entre bips

    uint8_t prevVol = M5.Speaker.getVolume();
    M5.Speaker.setVolume(255);  // Volume maximo para emergencia

    // 3 bips por ciclo (loop do main chama a cada 3s)
    M5.Speaker.tone(FREQ, BIP);
    M5.Speaker.tone(0, PAUSA);
    M5.Speaker.tone(FREQ, BIP);
    M5.Speaker.tone(0, PAUSA);
    M5.Speaker.tone(FREQ, BIP);

    // FIX B4: Restaurar volume original apos enfileirar os tons
    M5.Speaker.setVolume(prevVol);
}

/**
 * Atualiza estado do audio — DEVE ser chamado a cada loop().
 *
 * Processa:
 * 1. Deteccao de fim de audio (M5.Speaker.isPlaying() → false)
 * 2. Gap de 200ms entre arquivos na sequencia (frase composta)
 * 3. Maquina de estados do beep de erro (non-blocking)
 */
void AudioPlayer::update() {
    unsigned long now = millis();

    // Detectar fim do audio atual para processar sequencia
    // Quando _playing e true mas M5.Speaker parou, o arquivo terminou
    if (_playing && !M5.Speaker.isPlaying()) {
        _playing = false;

        if (_sequencePending && _queuedFile[0] != '\0') {
            // File1 terminou, tem file2 na fila — iniciar gap de 200ms
            _sequenceGapStartMs = millis();
        }
    }

    // Apos gap entre file1 e file2, tocar o proximo arquivo
    if (_sequencePending && _sequenceGapStartMs > 0 && !_playing) {
        if (now - _sequenceGapStartMs >= SEQUENCE_GAP_MS) {
            // Gap expirou — tocar arquivo da fila
            char queuedCopy[64];
            strncpy(queuedCopy, _queuedFile, sizeof(queuedCopy) - 1);
            queuedCopy[sizeof(queuedCopy) - 1] = '\0';

            _queuedFile[0] = '\0';
            _sequencePending = false;
            _sequenceGapStartMs = 0;

            playFile(queuedCopy);
            log_i("%s: Sequence file2 started: %s", TAG, queuedCopy);
        }
    }

    // Maquina de estados do beep de erro (non-blocking)
    if (_beepState == BEEP_IDLE) {
        return;
    }

    switch (_beepState) {
    case BEEP_ERROR_TONE1:
        // Esperar primeiro tom terminar (100 ms)
        if (now - _beepStartMs >= 100) {
            _beepState = BEEP_ERROR_PAUSE;
            _beepStartMs = now;
        }
        break;

    case BEEP_ERROR_PAUSE:
        // Esperar pausa (80 ms)
        if (now - _beepStartMs >= 80) {
            // Tocar segundo tom
            M5.Speaker.tone(500, 100);
            _beepState = BEEP_ERROR_TONE2;
            _beepStartMs = now;
        }
        break;

    case BEEP_ERROR_TONE2:
        // Esperar segundo tom terminar (100 ms)
        if (now - _beepStartMs >= 100) {
            _beepState = BEEP_IDLE;
        }
        break;

    default:
        _beepState = BEEP_IDLE;
        break;
    }
}

// ---------------------------------------------------------------------------
// Helpers privados
// ---------------------------------------------------------------------------

bool AudioPlayer::initSPIFFS() {
    // Tentar montar sem formatar primeiro.
    // SPIFFS.begin(true) formatava ao corromper — apagava TODOS os audios
    // silenciosamente, tornando o dispositivo mudo sem indicacao.
    if (!SPIFFS.begin(false)) {
        log_e("%s: SPIFFS mount failed — tentando format...", TAG);
        if (!SPIFFS.begin(true)) {
            log_e("%s: SPIFFS mount FAILED even after format", TAG);
            return false;
        }
        log_w("%s: SPIFFS foi formatado! Audios perdidos — necessario reflash.", TAG);
    }

    size_t totalBytes = SPIFFS.totalBytes();
    size_t usedBytes  = SPIFFS.usedBytes();
    log_i("%s: SPIFFS mounted — total: %u KB, used: %u KB",
          TAG, totalBytes / 1024, usedBytes / 1024);

    // Alerta se SPIFFS esta vazio (possivel corrupcao anterior formatou)
    if (usedBytes < 1024) {
        log_e("%s: SPIFFS quase vazio (%u bytes) — audios podem estar faltando!", TAG, usedBytes);
    }

    return true;
}
