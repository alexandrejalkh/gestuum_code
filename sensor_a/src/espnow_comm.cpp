/**
 * ============================================================
 * GESTUUM — ESP-NOW Communication (Sensor A)
 * ============================================================
 * Gerencia toda comunicacao ESP-NOW do firmware principal:
 * - Recebe dados IMU do Sensor B a 50Hz via double-buffer
 * - Envia heartbeat, categoria, gesto reconhecido, emergencia
 * - Auto-pareamento com Sensor B e AtomS3 LED
 *
 * CONCORRENCIA: O callback onDataRecv() roda na task WiFi do
 * ESP32 (nao e ISR puro, mas tem restricoes similares).
 * NAO pode: alocar memoria, usar Serial, acessar SPI/display.
 * SO pode: copiar dados para buffers volateis e setar flags.
 *
 * FIX C04: Adicionado spinlock (portMUX_TYPE) no double-buffer
 *          para evitar torn reads quando ISR dispara 2x seguidas.
 * FIX C05: Removido Serial.println e esp_now_add_peer de dentro
 *          do callback. Pairing agora e feito via flag no loop().
 * ============================================================
 */

#include "espnow_comm.h"

#include <M5StickCPlus2.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

// --- Estado global ---

volatile bool newIMUDataAvailable = false;

uint8_t sensorB_mac[6] = {0};
uint8_t broadcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
// FIX #3: volatile — escrito no loop (addSensorBPeer), lido no callback
volatile bool sensorBPaired = false;

// Timestamp do ultimo heartbeat do Sensor B (volatile — escrito no callback)
static volatile unsigned long lastHeartbeat = 0;

// --- Double-buffer para dados IMU ---
// Dois buffers IMUPacket: callback escreve em um, loop() le do outro.
// FIX C04: spinlock protege contra torn reads quando callback
// dispara 2x antes do loop() consumir (a cada 20ms a 50Hz).
static IMUPacket imuBuffers[2];
static volatile uint8_t writeIndex = 0;
static portMUX_TYPE imuMux = portMUX_INITIALIZER_UNLOCKED;

// FIX C05: Flag para pairing diferido — callback seta flag,
// loop() faz o pairing real (que usa Serial e esp_now_add_peer)
static volatile bool pairingNeeded = false;
static uint8_t pendingPairMac[6] = {0};

// FIX BUG-11: Flag de emergencia recebida do Sensor B.
// Callback seta a flag, loop() do main.cpp processa.
volatile bool emergencyFromSensorB = false;
// FIX A↔B #5: Spinlock para proteger pendingPairMac (6 bytes nao atomico)
static portMUX_TYPE pairMux = portMUX_INITIALIZER_UNLOCKED;

// Canal ESP-NOW cacheado — lido UMA vez no espnow_init(), nunca mais toca NVS.
// FIX SERIAL-01: getStoredChannel() abria NVS a cada pairing, causando contencao
// com a WiFi task e bloqueio do loop (serial morria).
static uint8_t cachedChannel = ESPNOW_CHANNEL;

// --- Helper interno: adiciona Sensor B como peer ---
// IMPORTANTE: so pode ser chamado do loop(), NUNCA do callback
static void addSensorBPeer(const uint8_t* mac) {
    memcpy(sensorB_mac, mac, 6);

    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, sensorB_mac, 6);
    // FIX BUG-04 + SERIAL-01: Usar canal cacheado (lido no init, sem NVS no loop)
    peerInfo.channel = cachedChannel;
    peerInfo.encrypt = false;

    if (!esp_now_is_peer_exist(sensorB_mac)) {
        esp_err_t err = esp_now_add_peer(&peerInfo);
        if (err == ESP_OK) {
            Serial.println("[ESPNOW] Sensor B peer added");
        } else {
            Serial.printf("[ESPNOW] add peer failed: %d\n", err);
        }
    }

    sensorBPaired = true;
    lastHeartbeat = millis();
}

// --- Callbacks ESP-NOW ---
// REGRA: callbacks rodam na task WiFi. So copiar dados e setar flags.
// Nada de Serial, SPI, malloc, esp_now_add_peer.

void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Nao usado pelo Sensor A — deixar vazio para evitar overhead
}

/**
 * Callback de recepcao ESP-NOW.
 * Roda na task WiFi — restricoes de ISR se aplicam.
 *
 * FIX C04: Usa spinlock no double-buffer para garantir atomicidade.
 * FIX C05: Ao inves de chamar addSensorBPeer() (que usa Serial),
 * salva o MAC e seta pairingNeeded = true para o loop() processar.
 */
void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) {
        return;
    }

    uint8_t type = data[0];

    if (type == MSG_IMU_DATA && len >= static_cast<int>(sizeof(IMUPacket))) {
        // FIX C04: Spinlock protege escrita + swap do writeIndex
        // Impede que um segundo disparo do callback corrompa o buffer
        // que o loop() esta lendo
        portENTER_CRITICAL_ISR(&imuMux);
        memcpy(&imuBuffers[writeIndex], data, sizeof(IMUPacket));
        writeIndex = 1 - writeIndex;
        newIMUDataAvailable = true;
        portEXIT_CRITICAL_ISR(&imuMux);

        // FIX C05: Pairing diferido — salva MAC para processar no loop()
        if (!sensorBPaired) {
            portENTER_CRITICAL_ISR(&pairMux);
            memcpy(pendingPairMac, mac, 6);
            pairingNeeded = true;
            portEXIT_CRITICAL_ISR(&pairMux);
        }
    }
    else if (type == MSG_HEARTBEAT && len >= static_cast<int>(sizeof(HeartbeatPacket))) {
        lastHeartbeat = millis();

        // FIX C05: Pairing diferido via flag
        if (!sensorBPaired) {
            portENTER_CRITICAL_ISR(&pairMux);
            memcpy(pendingPairMac, mac, 6);
            pairingNeeded = true;
            portEXIT_CRITICAL_ISR(&pairMux);
        }
    }
    // FIX BUG-11: Processar emergencia do Sensor B.
    // Antes deste fix, MSG_EMERGENCY era ignorado — SOS da mao direita nao funcionava.
    else if (type == MSG_EMERGENCY && len >= static_cast<int>(sizeof(EmergencyPacket))) {
        emergencyFromSensorB = true;
    }
}

// --- Initialization ---

// Le canal ESP-NOW do NVS e salva no cache. Chamado UMA vez no espnow_init().
static void loadChannelFromNVS() {
    Preferences prefs;
    uint8_t ch = ESPNOW_CHANNEL;
    if (prefs.begin("gestuum_ch", true)) {
        ch = prefs.getUChar("channel", ESPNOW_CHANNEL);
        prefs.end();
    }
    if (ch < 1 || ch > 13) ch = ESPNOW_CHANNEL;
    cachedChannel = ch;
}

// FIX P-Bug3 (2026-05-02 pentest Frank): getter runtime.
uint8_t espnow_get_channel() {
    return cachedChannel;
}

void espnow_init() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // FIX INSTALL-10: Aguardar WiFi STA inicializar completamente.
    // Sem esse delay, esp_wifi_set_channel() pode ser ignorado.
    delay(200);

    // FIX SERIAL-01: Ler canal do NVS UMA vez e cachear.
    // Antes: getStoredChannel() abria NVS a cada pairing no loop.
    loadChannelFromNVS();
    esp_wifi_set_channel(cachedChannel, WIFI_SECOND_CHAN_NONE);

    // FIX INSTALL-10: Verificar canal real aplicado
    uint8_t primary_ch = 0;
    wifi_second_chan_t second_ch;
    esp_wifi_get_channel(&primary_ch, &second_ch);
    Serial.printf("[ESPNOW] Canal configurado: %d | Canal real: %d\n", cachedChannel, primary_ch);

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] esp_now_init FAILED");
        return;
    }

    // Register send and receive callbacks
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // FIX INSTALL-10: Usar canal real (variavel) em vez de constante ESPNOW_CHANNEL
    esp_now_peer_info_t broadcastPeer;
    memset(&broadcastPeer, 0, sizeof(broadcastPeer));
    memcpy(broadcastPeer.peer_addr, broadcast_mac, 6);
    broadcastPeer.channel = cachedChannel;
    broadcastPeer.encrypt = false;

    if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
        Serial.println("[ESPNOW] add broadcast peer FAILED");
        return;
    }

    // Initialize double buffers
    memset(&imuBuffers[0], 0, sizeof(IMUPacket));
    memset(&imuBuffers[1], 0, sizeof(IMUPacket));
    writeIndex = 0;

    sensorBPaired = false;
    newIMUDataAvailable = false;

    Serial.println("[ESPNOW] initialized OK");
}

// --- Send functions ---

void espnow_send_heartbeat() {
    HeartbeatPacket pkt;
    pkt.type = MSG_HEARTBEAT;
    pkt.sender = 0;  // 0 = Sensor A
    // FIX #6: Clamp bateria para 0-100 (pode retornar negativo em alguns hardware)
    int32_t battRaw = M5.Power.getBatteryLevel();  // FIX INSTALL-11: SIOF
    if (battRaw < 0) battRaw = 0;
    if (battRaw > 100) battRaw = 100;
    pkt.battery = static_cast<uint8_t>(battRaw);

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(HeartbeatPacket));
}

void espnow_send_category(Category cat) {
    CategoryPacket pkt;
    pkt.type = MSG_CATEGORY;
    pkt.category = static_cast<uint8_t>(cat);

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(CategoryPacket));
}

void espnow_send_gesture(uint8_t category, uint16_t gestureId,
                          uint8_t automationCmd, bool active) {
    GesturePacket pkt;
    pkt.type = MSG_GESTURE;
    pkt.category = category;
    pkt.gesture_id = gestureId;
    pkt.automation_cmd = automationCmd;
    pkt.active = active ? 1 : 0;

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(GesturePacket));
}

void espnow_send_emergency(uint8_t level, uint16_t gestureId) {
    EmergencyPacket pkt;
    pkt.type = MSG_EMERGENCY;
    pkt.level = level;
    pkt.gesture_id = gestureId;

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(EmergencyPacket));
}

void espnow_send_automation(uint8_t command, uint8_t param) {
    AutomationPacket pkt;
    pkt.type = MSG_AUTOMATION;
    pkt.command = command;
    pkt.param = param;

    esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(AutomationPacket));
}

// --- Query functions ---

// --- Troca de canal ---

void espnow_set_channel(uint8_t newChannel) {
    if (newChannel < 1 || newChannel > 13) {
        Serial.printf("[ESPNOW] Canal invalido: %d\n", newChannel);
        return;
    }

    // Salva no NVS para persistir entre reboots
    Preferences prefs;
    if (prefs.begin("gestuum_ch", false)) {
        prefs.putUChar("channel", newChannel);
        prefs.end();
    }

    // Broadcast MSG_SET_CHANNEL no canal ATUAL para que B e AtomS3 recebam
    SetChannelPacket pkt;
    pkt.channel = newChannel;

    // Envia 3x para garantir entrega (broadcast sem ACK)
    for (int i = 0; i < 3; i++) {
        esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(SetChannelPacket));
        delay(50);
    }

    Serial.printf("[ESPNOW] Canal alterado para %d. Reiniciando...\n", newChannel);
    delay(500);
    ESP.restart();
}

// --- Funcoes de consulta ---

bool espnow_is_sensor_b_connected() {
    if (sensorBPaired && (millis() - lastHeartbeat >= CONNECTION_TIMEOUT_MS)) {
        // FIX #13: Reset pairing quando heartbeat expira.
        // Permite re-pairing com um Sensor B diferente (substituicao ou reboot).
        // Antes: sensorBPaired ficava true para sempre, bloqueando novo pairing.
        sensorBPaired = false;
        Serial.println("[ESPNOW] Sensor B connection lost — allowing re-pairing");
    }
    return sensorBPaired && (millis() - lastHeartbeat < CONNECTION_TIMEOUT_MS);
}

unsigned long espnow_last_imu_timestamp() {
    // Le do buffer oposto ao que o callback esta escrevendo
    portENTER_CRITICAL(&imuMux);
    uint8_t readIdx = 1 - writeIndex;
    unsigned long ts = imuBuffers[readIdx].timestamp;
    portEXIT_CRITICAL(&imuMux);
    return ts;
}

/**
 * Copia o ultimo pacote IMU completo para o buffer do caller.
 *
 * FIX C04: Usa spinlock para garantir que o callback nao
 * sobreescreva o buffer durante a leitura. Sem isso, se o
 * callback dispara 2x em sequencia rapida (50Hz = 20ms),
 * o writeIndex volta ao valor original e ambos os lados
 * leem/escrevem o mesmo buffer → dados corrompidos.
 *
 * Retorna true se havia dados novos, false caso contrario.
 */
bool espnow_getIMUData(IMUPacket* out) {
    if (!newIMUDataAvailable || out == nullptr) {
        return false;
    }

    // FIX C04: Secao critica protege leitura + clear do flag
    portENTER_CRITICAL(&imuMux);
    uint8_t readIdx = 1 - writeIndex;
    memcpy(out, &imuBuffers[readIdx], sizeof(IMUPacket));
    newIMUDataAvailable = false;
    portEXIT_CRITICAL(&imuMux);

    return true;
}

/**
 * FIX C05: Processa pairing diferido do Sensor B.
 * Deve ser chamado no loop() principal do main.cpp.
 *
 * O callback ESP-NOW nao pode chamar esp_now_add_peer() nem
 * Serial.println() porque roda na task WiFi. Entao ele seta
 * pairingNeeded=true e salva o MAC. Esta funcao faz o pairing
 * real, de forma segura, no contexto do loop().
 */
void espnow_processPairing() {
    if (pairingNeeded && !sensorBPaired) {
        // FIX A↔B #5: Copia MAC sob spinlock (6 bytes nao atomico)
        uint8_t mac[6];
        portENTER_CRITICAL(&pairMux);
        memcpy(mac, pendingPairMac, 6);
        pairingNeeded = false;
        portEXIT_CRITICAL(&pairMux);
        addSensorBPeer(mac);
    }
}
