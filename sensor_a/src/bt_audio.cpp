/**
 * GESTUUM — Bluetooth A2DP Audio Source Implementation
 *
 * Usa a API nativa do ESP-IDF para streaming A2DP.
 * O ESP32 atua como A2DP Source (envia audio) e a caixa de som
 * como A2DP Sink (recebe audio).
 *
 * Fluxo:
 *   1. begin() → inicializa BT controller + Bluedroid + A2DP source
 *   2. connectToSaved() → tenta conectar ao ultimo speaker salvo no NVS
 *   3. playPCM() → copia dados para ring buffer
 *   4. a2dpDataCallback() → BT stack le do ring buffer e transmite
 *
 * O callback A2DP espera stereo (2 canais). Como nosso audio e mono,
 * duplicamos cada sample para L+R no callback.
 */

#include "bt_audio.h"

#include <Arduino.h>
#include <Preferences.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_bt_device.h>
#include <esp_a2dp_api.h>
#include <esp_gap_bt_api.h>
#include <cstring>

static const char* TAG = "BTAudio";

// Instancia global (necessaria para callbacks estaticos)
static BTAudio* s_instance = nullptr;

// ---------------------------------------------------------------------------
// Construtor / Destrutor
// ---------------------------------------------------------------------------

BTAudio::BTAudio()
    : _initialized(false)
    , _connected(false)
    , _playing(false)
    , _hasSavedSpeaker(false)
    , _pcmMutex(nullptr)
    , _pcmData(nullptr)
    , _pcmTotalSamples(0)
    , _pcmReadPos(0)
{
    memset(_peerAddr, 0, sizeof(_peerAddr));
    // FIX AUDIT-01: Criar mutex para proteger buffer PCM entre tasks
    _pcmMutex = xSemaphoreCreateMutex();
    s_instance = this;
}

BTAudio::~BTAudio() {
    // FIX AUDIT-03: Liberar buffer PCM e mutex no destrutor
    if (_pcmData != nullptr) {
        free(_pcmData);
        _pcmData = nullptr;
    }
    if (_pcmMutex != nullptr) {
        vSemaphoreDelete(_pcmMutex);
        _pcmMutex = nullptr;
    }
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

// ---------------------------------------------------------------------------
// begin() — Inicializa Bluetooth Classic + A2DP Source
// ---------------------------------------------------------------------------

bool BTAudio::begin(const char* deviceName) {
    if (_initialized) {
        return true;
    }

    // Liberar memoria do BT Classic (ESP32 reserva ~60KB por padrao)
    // Se BLE ja esta em uso, o controller ja esta inicializado
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();

    // === PASSO 1: BT Controller ===
    // O M5Unified (via Arduino BLE) pode ter inicializado o controller em modo BLE-only.
    // Para A2DP precisamos de modo BTDM (BLE + Classic). Se ja esta enabled em modo
    // errado, precisamos desabilitar, deinit, e reinicializar em BTDM.
    esp_bt_controller_status_t ctrlStatus = esp_bt_controller_get_status();
    Serial.printf("[BT] Controller status inicial: %d (0=IDLE, 1=INITED, 2=ENABLED)\n", ctrlStatus);

    if (ctrlStatus == ESP_BT_CONTROLLER_STATUS_ENABLED) {
        // Controller ja enabled (provavelmente em modo BLE-only pelo Arduino/M5Unified)
        // Precisamos reinicializar em modo BTDM
        Serial.println("[BT] Controller already enabled — disabling to switch to BTDM...");
        esp_bt_controller_disable();
        esp_bt_controller_deinit();
        ctrlStatus = ESP_BT_CONTROLLER_STATUS_IDLE;
    }

    if (ctrlStatus == ESP_BT_CONTROLLER_STATUS_IDLE) {
        Serial.println("[BT] Controller IDLE — initializing...");
        esp_err_t err = esp_bt_controller_init(&bt_cfg);
        Serial.printf("[BT] Controller init: %s (%d)\n", err == ESP_OK ? "OK" : "FAILED", err);
        if (err != ESP_OK) return false;
    } else {
        Serial.printf("[BT] Controller NOT idle (status=%d) — skip init\n", ctrlStatus);
    }

    ctrlStatus = esp_bt_controller_get_status();
    Serial.printf("[BT] Controller status after init: %d\n", ctrlStatus);

    if (ctrlStatus == ESP_BT_CONTROLLER_STATUS_INITED) {
        Serial.println("[BT] Enabling BTDM mode...");
        esp_err_t err = esp_bt_controller_enable(ESP_BT_MODE_BTDM);
        Serial.printf("[BT] Controller enable BTDM: %s (%d)\n", err == ESP_OK ? "OK" : "FAILED", err);
        if (err != ESP_OK) return false;
    } else {
        Serial.printf("[BT] Controller NOT in INITED state (status=%d) — skip enable\n", ctrlStatus);
    }

    // === PASSO 2: Bluedroid Stack ===
    esp_bluedroid_status_t bdStatus = esp_bluedroid_get_status();
    Serial.printf("[BT] Bluedroid status: %d (0=UNINIT, 1=INITED, 2=ENABLED)\n", bdStatus);

    if (bdStatus == ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        esp_err_t err = esp_bluedroid_init();
        Serial.printf("[BT] Bluedroid init: %s (%d)\n", err == ESP_OK ? "OK" : "FAILED", err);
        if (err != ESP_OK) return false;
    }

    bdStatus = esp_bluedroid_get_status();
    if (bdStatus == ESP_BLUEDROID_STATUS_INITIALIZED) {
        esp_err_t err = esp_bluedroid_enable();
        Serial.printf("[BT] Bluedroid enable: %s (%d)\n", err == ESP_OK ? "OK" : "FAILED", err);
        if (err != ESP_OK) return false;
    } else {
        Serial.printf("[BT] Bluedroid already enabled (status=%d)\n", bdStatus);
    }

    // === PASSO 3: Nome do dispositivo ===
    esp_bt_dev_set_device_name(deviceName);
    Serial.printf("[BT] Device name set to: %s\n", deviceName);

    // === PASSO 4: Registrar callbacks ===
    esp_a2d_source_register_data_callback(a2dpDataCallback);
    esp_a2d_register_callback(a2dpCallback);
    esp_bt_gap_register_callback(gapCallback);
    Serial.println("[BT] Callbacks registered (A2DP + GAP)");

    // === PASSO 5: A2DP Source init ===
    esp_err_t a2dpErr = esp_a2d_source_init();
    Serial.printf("[BT] A2DP source init: %s (%d)\n", a2dpErr == ESP_OK ? "OK" : "FAILED", a2dpErr);
    if (a2dpErr != ESP_OK) return false;

    // === PASSO 6: Seguranca SSP ===
    // IO_CAP_NONE = "Just Works" — funciona com qualquer speaker sem confirmacao.
    // TODO ALT-10: Trocar para IO_CAP_OUT quando implementar confirmacao no display.
    // IO_CAP_OUT quebrou pairing com speakers que nao suportam confirmacao numerica.
    esp_bt_sp_param_t param_type = ESP_BT_SP_IOCAP_MODE;
    esp_bt_io_cap_t iocap = ESP_BT_IO_CAP_NONE;
    esp_bt_gap_set_security_param(param_type, &iocap, sizeof(uint8_t));

    // Tornar o dispositivo descobrivel e conectavel
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
    Serial.println("[BT] Scan mode set: connectable + discoverable");

    _initialized = true;
    _hasSavedSpeaker = loadSavedSpeaker();

    log_i("%s: initialized OK (saved speaker: %s)", TAG,
          _hasSavedSpeaker ? "yes" : "no");
    return true;
}

// ---------------------------------------------------------------------------
// Conexao
// ---------------------------------------------------------------------------

void BTAudio::connectToSaved() {
    if (!_initialized || !_hasSavedSpeaker) {
        return;
    }

    log_i("%s: connecting to saved speaker %02X:%02X:%02X:%02X:%02X:%02X",
          TAG, _peerAddr[0], _peerAddr[1], _peerAddr[2],
          _peerAddr[3], _peerAddr[4], _peerAddr[5]);

    esp_a2d_source_connect(_peerAddr);
}

bool BTAudio::discoverAndConnect(uint32_t timeoutMs) {
    if (!_initialized) {
        return false;
    }

    Serial.printf("[BT] Starting discovery (%lu ms timeout)...\n", timeoutMs);

    // Iniciar descoberta GAP — resultados chegam via gapCallback
    uint8_t inqLen = timeoutMs / 1280 + 1;  // Unidade de 1.28s
    if (inqLen > 48) inqLen = 48;           // Max permitido pelo ESP-IDF
    esp_err_t err = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY,
                                               inqLen, 0);
    Serial.printf("[BT] gap_start_discovery: %s (%d), inqLen=%d\n",
                  err == ESP_OK ? "OK" : "FAILED", err, inqLen);

    // Esperar conexao (bloqueante)
    unsigned long start = millis();
    while (!_connected && (millis() - start) < timeoutMs) {
        delay(100);
    }

    if (_connected) {
        saveCurrentSpeaker();
        log_i("%s: connected and saved", TAG);
    } else {
        log_w("%s: discovery timeout — no speaker found", TAG);
    }

    return _connected;
}

void BTAudio::disconnect() {
    if (_connected) {
        esp_a2d_source_disconnect(_peerAddr);
        _connected = false;
        _playing = false;
    }
}

// ---------------------------------------------------------------------------
// Streaming de audio
// ---------------------------------------------------------------------------

bool BTAudio::playPCM(const int16_t* data, size_t numSamples, uint32_t sampleRate) {
    if (!_initialized || !_connected) {
        return false;
    }

    // Alocar novo buffer ANTES de pegar o mutex (malloc pode demorar)
    size_t dataBytes = numSamples * sizeof(int16_t);
    int16_t* newBuf = (int16_t*)ps_malloc(dataBytes);
    if (newBuf == nullptr) {
        newBuf = (int16_t*)malloc(dataBytes);
    }
    if (newBuf == nullptr) {
        Serial.printf("[BT] Failed to allocate %d bytes for A2DP buffer\n", dataBytes);
        return false;
    }
    memcpy(newBuf, data, dataBytes);

    // FIX AUDIT-01: Mutex protege a troca de buffer.
    // O callback A2DP (outra task) pode estar lendo _pcmData neste instante.
    // Sem mutex, free() aqui + read() no callback = use-after-free.
    if (xSemaphoreTake(_pcmMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        // Liberar buffer anterior (callback nao esta lendo — mutex garante)
        if (_pcmData != nullptr) {
            free(_pcmData);
        }
        _pcmData = newBuf;
        _pcmReadPos = 0;
        _pcmTotalSamples = numSamples;
        _playing = true;
        xSemaphoreGive(_pcmMutex);
    } else {
        // Timeout no mutex — liberar buffer novo e desistir
        free(newBuf);
        Serial.println("[BT] playPCM: mutex timeout — skipped");
        return false;
    }

    Serial.printf("[BT] playPCM: %d samples queued for A2DP\n", numSamples);
    return true;
}

void BTAudio::stop() {
    // FIX AUDIT-01: Mutex garante que o callback nao esta lendo durante stop
    if (_pcmMutex != nullptr && xSemaphoreTake(_pcmMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _playing = false;
        _pcmReadPos = 0;
        _pcmTotalSamples = 0;
        xSemaphoreGive(_pcmMutex);
    } else {
        // Fallback sem mutex (nao deveria acontecer)
        _playing = false;
        _pcmReadPos = 0;
        _pcmTotalSamples = 0;
    }
}

bool BTAudio::isConnected() const {
    return _connected;
}

bool BTAudio::isPlaying() const {
    return _playing && _pcmReadPos < _pcmTotalSamples;
}

void BTAudio::update() {
    // Detectar fim do audio (callback ja leu tudo)
    if (_playing && _pcmReadPos >= _pcmTotalSamples) {
        _playing = false;
    }
}

// ---------------------------------------------------------------------------
// Callback GAP — processa resultados de descoberta de dispositivos BT
// ---------------------------------------------------------------------------

/**
 * O BT stack chama esta funcao quando encontra dispositivos durante
 * a descoberta (esp_bt_gap_start_discovery). Filtra por Class of Device
 * para identificar caixas de som (A2DP Sink) e conecta automaticamente.
 *
 * COD (Class of Device) para audio:
 *   Major class 0x04 = Audio/Video
 *   Minor classes relevantes: 0x04 (speaker), 0x18 (headphones),
 *   0x20 (car audio), 0x24 (portable audio)
 */
void BTAudio::gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param) {
    if (s_instance == nullptr) return;

    switch (event) {
    case ESP_BT_GAP_DISC_RES_EVT: {
        // Dispositivo encontrado na descoberta
        uint8_t* bda = param->disc_res.bda;
        char name[64] = "unknown";
        bool isAudio = false;

        // Extrair nome e classe do dispositivo dos properties
        for (int i = 0; i < param->disc_res.num_prop; i++) {
            esp_bt_gap_dev_prop_t* prop = &param->disc_res.prop[i];

            if (prop->type == ESP_BT_GAP_DEV_PROP_BDNAME && prop->len > 0) {
                int len = (prop->len < (int)sizeof(name) - 1) ? prop->len : (int)sizeof(name) - 1;
                memcpy(name, prop->val, len);
                name[len] = '\0';
            }

            if (prop->type == ESP_BT_GAP_DEV_PROP_COD) {
                uint32_t cod = *(uint32_t*)(prop->val);
                // Major service class: bit 21 = Audio
                // Major device class: bits 12-8, 0x04 = Audio/Video
                uint8_t majorDeviceClass = (cod >> 8) & 0x1F;
                bool audioService = (cod >> 21) & 1;

                if (majorDeviceClass == 0x04 || audioService) {
                    isAudio = true;
                }
            }
        }

        // FIX BT-CONN: log_d() em vez de Serial.printf() — callbacks BT rodam
        // na task BTC com stack limitado. Serial.printf() bloqueia no UART TX
        // e causa timeout no handshake de autenticacao SSP.
        // Com CORE_DEBUG_LEVEL=1, log_d() e NO-OP (zero overhead).
        log_d("%s: Found: %s [%02X:%02X:%02X:%02X:%02X:%02X] audio=%s",
              TAG, name, bda[0], bda[1], bda[2], bda[3], bda[4], bda[5],
              isAudio ? "YES" : "no");

        // Se e dispositivo de audio e ainda nao estamos conectados, conectar
        if (isAudio && !s_instance->_connected) {
            log_d("%s: Connecting to %s...", TAG, name);
            // Parar descoberta antes de conectar
            esp_bt_gap_cancel_discovery();
            memcpy(s_instance->_peerAddr, bda, 6);
            esp_a2d_source_connect(bda);
        }
        break;
    }

    case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
        if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
            log_i("%s: Discovery stopped", TAG);
        } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
            log_i("%s: Discovery started", TAG);
        }
        break;
    }

    case ESP_BT_GAP_AUTH_CMPL_EVT: {
        // Autenticacao SSP completada
        if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
            log_i("%s: Auth OK: %s", TAG, param->auth_cmpl.device_name);
        } else {
            // Auth failure e importante — log_w aparece com CORE_DEBUG_LEVEL>=2
            log_w("%s: Auth FAILED, status=%d", TAG, param->auth_cmpl.stat);
        }
        break;
    }

    case ESP_BT_GAP_CFM_REQ_EVT: {
        // Confirmacao numerica — aceitar automaticamente (SSP "Numeric Comparison")
        log_d("%s: Numeric confirm: %lu — accepting", TAG, param->cfm_req.num_val);
        esp_bt_gap_ssp_confirm_reply(param->cfm_req.bda, true);
        break;
    }

    case ESP_BT_GAP_KEY_REQ_EVT: {
        // Passkey request — responder 0 (SSP "Passkey Entry")
        log_d("%s: Passkey request — replying 0", TAG);
        esp_bt_gap_ssp_passkey_reply(param->key_req.bda, true, 0);
        break;
    }

    case ESP_BT_GAP_PIN_REQ_EVT: {
        // PIN request (caixas BT 2.0 legacy) — responder com "0000"
        log_d("%s: PIN request — replying 0000", TAG);
        esp_bt_pin_code_t pin = {'0', '0', '0', '0'};
        esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin);
        break;
    }

    default:
        // FIX BT-CONN: NAO usar Serial.printf() no default — este case dispara
        // para DEZENAS de eventos GAP durante o handshake (ACL complete, link key,
        // mode change, etc.). Serial a 115200 baud bloqueia ~2ms por evento.
        // Rajada de 5-10 eventos = 10-20ms de bloqueio na task BTC, causando
        // timeout no SSP e desconexao imediata (estado 1->0).
        log_v("%s: GAP event: %d", TAG, event);
        break;
    }
}

// ---------------------------------------------------------------------------
// Callback A2DP — chamado pelo BT stack quando precisa de dados
// ---------------------------------------------------------------------------

/**
 * O BT stack chama esta funcao periodicamente pedindo dados PCM.
 * Formato esperado: stereo interleaved 16-bit (L, R, L, R...).
 * Como nosso audio e mono, duplicamos cada sample para L e R.
 *
 * @param data Buffer de saida para preencher.
 * @param len Tamanho em bytes que o stack quer (sempre multiplo de 4).
 * @return Numero de bytes escritos.
 */
int32_t BTAudio::a2dpDataCallback(uint8_t* data, int32_t len) {
    if (s_instance == nullptr || !s_instance->_playing ||
        s_instance->_pcmData == nullptr) {
        // Silencio quando nao ha dados
        memset(data, 0, len);
        return len;
    }

    // FIX AUDIT-01: Mutex protege leitura do buffer PCM.
    // O callback roda na task BT — sem mutex, playPCM() pode liberar
    // o buffer enquanto este loop esta lendo.
    // Timeout curto (5ms) para nao bloquear o stack BT.
    if (xSemaphoreTake(s_instance->_pcmMutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        // Nao conseguiu o mutex — enviar silencio neste ciclo
        memset(data, 0, len);
        return len;
    }

    // len e em bytes, cada frame stereo = 4 bytes (2 × int16_t)
    int32_t frames = len / 4;
    int16_t* out = (int16_t*)data;

    for (int32_t i = 0; i < frames; i++) {
        int16_t sample = 0;
        if (s_instance->_pcmReadPos < s_instance->_pcmTotalSamples) {
            sample = s_instance->_pcmData[s_instance->_pcmReadPos];
            s_instance->_pcmReadPos++;
        }
        // Mono → stereo (duplicar para L e R)
        out[i * 2]     = sample;  // Left
        out[i * 2 + 1] = sample;  // Right
    }

    xSemaphoreGive(s_instance->_pcmMutex);
    return len;
}

// ---------------------------------------------------------------------------
// Callback de eventos A2DP e GAP
// ---------------------------------------------------------------------------

void BTAudio::a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param) {
    if (s_instance == nullptr) return;

    switch (event) {
    case ESP_A2D_CONNECTION_STATE_EVT: {
        uint8_t state = param->conn_stat.state;
        // FIX BT-CONN: log_d() em vez de Serial.printf() — mesmo motivo do gapCallback.
        // Este callback tambem roda na task BTC do ESP-IDF.
        log_d("%s: A2DP conn state: %d", TAG, state);
        if (state == ESP_A2D_CONNECTION_STATE_CONNECTED) {
            // Conectado ao speaker — salvar MAC e iniciar media check
            memcpy(s_instance->_peerAddr, param->conn_stat.remote_bda, 6);
            s_instance->_connected = true;
            log_i("%s: A2DP connected to %02X:%02X:%02X:%02X:%02X:%02X",
                  TAG, s_instance->_peerAddr[0], s_instance->_peerAddr[1],
                  s_instance->_peerAddr[2], s_instance->_peerAddr[3],
                  s_instance->_peerAddr[4], s_instance->_peerAddr[5]);
            // Verificar se a fonte (nos) esta pronta para enviar audio
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY);
        } else if (state == ESP_A2D_CONNECTION_STATE_DISCONNECTED) {
            s_instance->_connected = false;
            s_instance->_playing = false;
            log_i("%s: A2DP disconnected", TAG);
        }
        break;
    }

    case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
        // Resposta ao media control (CHECK_SRC_RDY ou START)
        uint8_t cmd = param->media_ctrl_stat.cmd;
        uint8_t status = param->media_ctrl_stat.status;
        log_d("%s: Media ctrl ACK: cmd=%d, status=%d", TAG, cmd, status);

        if (cmd == ESP_A2D_MEDIA_CTRL_CHECK_SRC_RDY &&
            status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            // Fonte pronta — iniciar streaming
            log_i("%s: Source ready — starting media", TAG);
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
        } else if (cmd == ESP_A2D_MEDIA_CTRL_START &&
                   status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
            log_i("%s: Media stream started", TAG);
        }
        break;
    }

    case ESP_A2D_AUDIO_STATE_EVT: {
        uint8_t state = param->audio_stat.state;
        if (state == ESP_A2D_AUDIO_STATE_STARTED) {
            log_i("%s: Audio streaming ACTIVE", TAG);
        } else if (state == ESP_A2D_AUDIO_STATE_STOPPED) {
            log_i("%s: Audio streaming STOPPED", TAG);
        }
        break;
    }

    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// NVS — salvar/carregar MAC do speaker
// ---------------------------------------------------------------------------

bool BTAudio::loadSavedSpeaker() {
    Preferences prefs;
    if (!prefs.begin("gestuum_bt", true)) {  // read-only
        return false;
    }
    size_t len = prefs.getBytes("spk_mac", _peerAddr, 6);
    prefs.end();

    if (len != 6) {
        memset(_peerAddr, 0, 6);
        return false;
    }

    // Verificar se MAC nao e zero
    bool allZero = true;
    for (int i = 0; i < 6; i++) {
        if (_peerAddr[i] != 0) { allZero = false; break; }
    }
    return !allZero;
}

void BTAudio::saveSpeaker(const uint8_t* addr) {
    Preferences prefs;
    if (prefs.begin("gestuum_bt", false)) {  // read-write
        prefs.putBytes("spk_mac", addr, 6);
        prefs.end();
        log_i("%s: speaker MAC saved to NVS", TAG);
    }
}

void BTAudio::saveCurrentSpeaker() {
    if (_connected) {
        saveSpeaker(_peerAddr);
    }
}
