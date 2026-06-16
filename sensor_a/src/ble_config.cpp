/**
 * GESTUUM — BLE Configuration Service Implementation
 * Responsibility: ESP32 BLE GATT server for app-based configuration.
 *
 * Uses the standard ESP32 BLE Arduino library (BLEDevice, BLEServer, etc.).
 * Creates a single GATT service with three characteristics:
 *   - CMD_TX (Write): receives JSON commands from the mobile app
 *   - CMD_RX (Read+Notify): sends JSON responses back to the app
 *   - AUDIO_TX (Write): receives audio upload data chunks
 *
 * The command characteristic accepts JSON strings. When a complete JSON
 * string is received, the registered CommandCallback is invoked.
 */

#include "ble_config.h"
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// === Global instance ===
BLEConfig bleConfig;

// === Static pointers for BLE objects ===
static BLEServer* pServer = nullptr;
static BLEService* pService = nullptr;
static BLECharacteristic* pCmdTxChar = nullptr;   // App writes commands here
static BLECharacteristic* pCmdRxChar = nullptr;    // Sensor sends responses here
static BLECharacteristic* pAudioTxChar = nullptr;  // App writes audio data here

// === Estado interno para roteamento de callbacks ===
// FIX M14: volatile — estas variaveis sao escritas pelos callbacks BLE
// (que rodam na task BLE) e lidas pelo loop() (task Arduino).
// Sem volatile, compilador pode cachear valor em registrador.
static BLEConfig::CommandCallback s_cmdCallback = nullptr;
static volatile bool s_clientConnected = false;
static volatile bool s_uploadInProgress = false;

static const char* TAG = "BLEConfig";

// ============================================================================
// BLE Server Callbacks
// ============================================================================

class GestuumServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* server) override {
        s_clientConnected = true;
        Serial.printf("[%s] Client connected\n", TAG);
    }

    void onDisconnect(BLEServer* server) override {
        s_clientConnected = false;
        s_uploadInProgress = false;
        Serial.printf("[%s] Client disconnected\n", TAG);

        // FIX 2026-05-01 (item 3 plano B): so re-anuncia se BLE ainda esta
        // ativo. Se bleConfig.stop() foi chamado (transicao pra treino), o
        // _active vira false ANTES do disconnect chegar — neste caso, NAO
        // tentar startAdvertising porque o stack esta sendo desligado e gera
        // assert/crash em osi_thread_post_event (thread.c:431).
        if (server != nullptr && bleConfig.isActive()) {
            server->startAdvertising();
            Serial.printf("[%s] Advertising restarted\n", TAG);
        } else {
            Serial.printf("[%s] BLE sendo desligado, skip restart advertising\n", TAG);
        }
    }
};

// ============================================================================
// CMD_TX Characteristic Callbacks (App -> Sensor: JSON commands)
// ============================================================================

// FIX P3: Callback BLE salva comando no buffer ao inves de processar.
// onWrite roda na task BLE — chamar handleCommand aqui causaria race
// com audioPlayer.loop() e displayUI.update() que rodam no loop().
// O comando e processado em update() (contexto do loop — seguro).
//
// FIX 2026-05-01 (chunks BLE): MTU padrao Web Bluetooth = 23 bytes (20 uteis).
// Comandos JSON > 20 bytes chegam fragmentados em multiplos onWrite.
// Antes: cada chunk substituia o buffer e marcava pendente -> handleCommand
// recebia chunk solto -> "JSON parse error: EmptyInput / IncompleteInput".
// Agora: agrega chunks ate receber '\n' (terminador do protocolo PWA);
// so entao sinaliza pendente.
class CmdTxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.empty()) return;

        size_t curLen = strlen(bleConfig._cmdBuffer);
        size_t spaceLeft = BLEConfig::CMD_BUFFER_SIZE - curLen - 1;
        size_t copyLen = (value.length() < spaceLeft) ? value.length() : spaceLeft;

        memcpy(bleConfig._cmdBuffer + curLen, value.c_str(), copyLen);
        bleConfig._cmdBuffer[curLen + copyLen] = '\0';

        // So sinaliza pendente quando recebeu newline (linha completa)
        if (memchr(bleConfig._cmdBuffer, '\n', curLen + copyLen)) {
            bleConfig._cmdPending = true;
        }
    }
};

// ============================================================================
// AUDIO_TX Characteristic Callbacks (App -> Sensor: audio upload chunks)
// ============================================================================

// FIX P3: Audio chunks tambem diferidos para o loop()
class AudioTxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string value = pCharacteristic->getValue();
        if (value.length() > 0 && value.length() < BLEConfig::CMD_BUFFER_SIZE) {
            memcpy(bleConfig._cmdBuffer, value.c_str(), value.length());
            bleConfig._cmdBuffer[value.length()] = '\0';
            bleConfig._cmdPending = true;
        }
    }
};

// ============================================================================
// begin() — Initialize BLE server and start advertising
// ============================================================================

void BLEConfig::begin(const char* deviceName) {
    if (_active) {
        Serial.printf("[%s] Already active, ignoring begin()\n", TAG);
        return;
    }

    Serial.printf("[%s] Starting BLE with name: %s\n", TAG, deviceName);

    // Initialize BLE device
    BLEDevice::init(deviceName);

    // FIX C09: Negociar MTU maior para permitir respostas JSON grandes
    // iOS tipicamente aceita ate 517, Android varia
    BLEDevice::setMTU(517);

    // Create BLE server
    // FIX H09: Callbacks static para evitar memory leak em ciclos begin/stop.
    // Antes: new a cada begin() sem delete → leak de 3 objetos por ciclo.
    static GestuumServerCallbacks serverCb;
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(&serverCb);

    // Create GATT service
    pService = pServer->createService(GESTUUM_SERVICE_UUID);

    // --- CMD_TX characteristic (App writes JSON commands) ---
    pCmdTxChar = pService->createCharacteristic(
        GESTUUM_CMD_TX_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    // FIX H09: Callback static para evitar leak
    static CmdTxCallbacks cmdTxCb;
    pCmdTxChar->setCallbacks(&cmdTxCb);

    // --- CMD_RX characteristic (Sensor sends JSON responses, app reads/subscribes) ---
    pCmdRxChar = pService->createCharacteristic(
        GESTUUM_CMD_RX_UUID,
        BLECharacteristic::PROPERTY_READ |
        BLECharacteristic::PROPERTY_NOTIFY
    );
    // FIX P4: BLE2902 static para evitar leak em ciclos begin/stop.
    // Antes: new BLE2902() a cada begin() sem delete.
    static BLE2902 ble2902Desc;
    pCmdRxChar->addDescriptor(&ble2902Desc);

    // --- AUDIO_TX characteristic (App writes audio data chunks) ---
    pAudioTxChar = pService->createCharacteristic(
        GESTUUM_AUDIO_TX_UUID,
        BLECharacteristic::PROPERTY_WRITE |
        BLECharacteristic::PROPERTY_WRITE_NR
    );
    // FIX H09: Callback static para evitar leak
    static AudioTxCallbacks audioTxCb;
    pAudioTxChar->setCallbacks(&audioTxCb);

    // Start service
    pService->start();

    // Start advertising
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(GESTUUM_SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    // FIX H08: Segundo era setMinPreferred (sobrescrevia o primeiro).
    // setMinPreferred = intervalo minimo (7.5ms), setMaxPreferred = maximo (22.5ms)
    // Necessario para compatibilidade com iOS (Apple exige intervalo especifico)
    pAdvertising->setMinPreferred(0x06);  // Min interval: 7.5ms
    pAdvertising->setMaxPreferred(0x12);  // Max interval: 22.5ms
    BLEDevice::startAdvertising();

    _active = true;
    _clientConnected = false;
    _uploadInProgress = false;

    Serial.printf("[%s] BLE server started, advertising...\n", TAG);
}

// ============================================================================
// stop() — Cleanly shut down BLE
// ============================================================================

void BLEConfig::stop() {
    if (!_active) {
        return;
    }

    Serial.printf("[%s] Stopping BLE...\n", TAG);

    // FIX 2026-05-01 (item 3 plano B): marca _active=false ANTES de tocar
    // no stack. Quando o cliente desconectar (forcado por deinit), o callback
    // onDisconnect verificara isActive() e NAO tentara restartAdvertising —
    // que causava assert/crash em osi_thread_post_event durante shutdown
    // do Bluedroid stack.
    _active = false;
    _clientConnected = false;
    _uploadInProgress = false;
    s_clientConnected = false;
    s_uploadInProgress = false;

    // Stop advertising
    BLEDevice::getAdvertising()->stop();

    // Deinitialize BLE (frees all resources)
    BLEDevice::deinit(true);

    pServer = nullptr;
    pService = nullptr;
    pCmdTxChar = nullptr;
    pCmdRxChar = nullptr;
    pAudioTxChar = nullptr;

    // FIX L17: Nullar callback para evitar invocacao apos stop
    s_cmdCallback = nullptr;

    Serial.printf("[%s] BLE stopped\n", TAG);
}

// ============================================================================
// isActive()
// ============================================================================

bool BLEConfig::isActive() const {
    return _active;
}

// ============================================================================
// isClientConnected()
// ============================================================================

bool BLEConfig::isClientConnected() const {
    return s_clientConnected;
}

// ============================================================================
// update() — Called in loop to sync internal state
// ============================================================================

void BLEConfig::update() {
    if (!_active) {
        return;
    }

    // Sync connected state from static callback variable
    _clientConnected = s_clientConnected;
    _uploadInProgress = s_uploadInProgress;

    // FIX P3: Despacha comando diferido do buffer BLE.
    // O callback BLE salvou o comando em _cmdBuffer. Agora, no contexto
    // do loop() (task Arduino), e seguro chamar handleCommand — que acessa
    // SPIFFS, audioPlayer, display sem race conditions.
    //
    // FIX 2026-05-01 (chunks BLE): zerar o buffer ANTES do callback
    // pra que o proximo comando comece com buffer limpo (a agregacao
    // em CmdTxCallbacks::onWrite usa strlen — buffer sujo agregaria no antigo).
    if (_cmdPending && s_cmdCallback != nullptr) {
        _cmdPending = false;
        char cmdCopy[CMD_BUFFER_SIZE];
        memcpy(cmdCopy, _cmdBuffer, CMD_BUFFER_SIZE);
        _cmdBuffer[0] = '\0';   // limpa pro proximo comando
        s_cmdCallback(cmdCopy);
    }
}

// ============================================================================
// setCommandCallback()
// ============================================================================

void BLEConfig::setCommandCallback(CommandCallback cb) {
    _cmdCallback = cb;
    s_cmdCallback = cb;
}

// ============================================================================
// sendResponse() — Send JSON response to connected app via CMD_RX notify
// ============================================================================

/**
 * Envia resposta JSON para o app conectado via BLE notify.
 *
 * FIX C09: BLE MTU padrao = 23 bytes (20 uteis). Respostas JSON
 * podem ter ate 2048 bytes. Antes, o setValue enviava tudo de uma
 * vez e o stack BLE truncava silenciosamente.
 *
 * Agora: tenta negociar MTU 517 no begin(), e fragmenta a resposta
 * em chunks de (MTU - 3) bytes caso necessario. O app deve
 * reassemblar os chunks ate receber o ultimo (menor que chunk size).
 */
void BLEConfig::sendResponse(const char* json) {
    if (!_active || pCmdRxChar == nullptr) {
        Serial.printf("[%s] Cannot send response: BLE not active\n", TAG);
        return;
    }

    if (!s_clientConnected) {
        Serial.printf("[%s] Cannot send response: no client connected\n", TAG);
        return;
    }

    // FIX BLE-A: Adicionar \n como delimitador para protocolo newline-delimited do PWA.
    // Sem \n, o PWA nao sabe onde o JSON termina e chunks ficam acumulando no buffer.
    size_t jsonLen = strlen(json);
    char* toSend = (char*)malloc(jsonLen + 2);
    if (!toSend) {
        Serial.printf("[%s] malloc failed for response\n", TAG);
        return;
    }
    memcpy(toSend, json, jsonLen);
    toSend[jsonLen] = '\n';
    toSend[jsonLen + 1] = '\0';
    size_t totalLen = jsonLen + 1;

    // FIX P2: Usar MTU real negociado
    uint16_t mtu = BLEDevice::getMTU();
    if (mtu == 0 || mtu < 23) mtu = 23;
    uint16_t chunkSize = mtu - 3;
    if (chunkSize < 20) chunkSize = 20;

    if (totalLen <= chunkSize) {
        pCmdRxChar->setValue((uint8_t*)toSend, totalLen);
        pCmdRxChar->notify();
    } else {
        size_t offset = 0;
        while (offset < totalLen) {
            size_t remaining = totalLen - offset;
            size_t sendLen = (remaining > chunkSize) ? chunkSize : remaining;
            pCmdRxChar->setValue((uint8_t*)(toSend + offset), sendLen);
            pCmdRxChar->notify();
            offset += sendLen;
            delay(20);
        }
    }

    free(toSend);

    Serial.printf("[%s] Response sent (%d bytes)\n", TAG, totalLen);
}

// ============================================================================
// isUploadInProgress()
// ============================================================================

bool BLEConfig::isUploadInProgress() const {
    return s_uploadInProgress;
}
