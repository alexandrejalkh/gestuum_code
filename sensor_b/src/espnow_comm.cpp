/**
 * ============================================================
 * GESTUUM — ESP-NOW Communication (Sensor B)
 * ============================================================
 * Gerencia comunicacao ESP-NOW do sensor secundario:
 * - Recebe heartbeat do Sensor A (descobre MAC, confirma conexao)
 * - Recebe mudanca de categoria do Sensor A
 * - Envia dados IMU a 50Hz para o Sensor A
 * - Envia heartbeat de volta para Sensor A
 *
 * FIX C07: Removido Serial.println() e updateDisplay() de dentro
 *          do callback onDataRecv(). Callback roda na task WiFi —
 *          Serial usa mutex (deadlock), display usa SPI (crash).
 *          Agora so seta flags volateis; processamento no loop().
 *
 * FIX C08: Variaveis compartilhadas agora sao volatile (config.h).
 *          sensorA_mac protegido por spinlock (6 bytes nao atomicos).
 * ============================================================
 */

#include "espnow_comm.h"
#include "config.h"
#include "constants.h"

#include <M5StickCPlus2.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include <Preferences.h>
#include <string.h>

// Rate-limit aviso de desconexao no Serial (1x por segundo)
static unsigned long lastDisconnectedWarning = 0;

// FIX C08: Spinlock para proteger sensorA_mac (6 bytes = nao atomico)
// FIX INSTALL-09: Removido 'static' — main.cpp tambem usa macMux
portMUX_TYPE macMux = portMUX_INITIALIZER_UNLOCKED;

// FIX C07: Flag para peer add diferido (nao pode no callback)
static volatile bool peerAddNeeded = false;
static uint8_t pendingMac[6] = {0};
// FIX A↔B #5: Spinlock para pendingMac (6 bytes nao atomico)
static portMUX_TYPE pairMux = portMUX_INITIALIZER_UNLOCKED;

// FIX BUG-08: Flag para troca de canal diferida.
static volatile bool channelChangeNeeded = false;
static volatile uint8_t pendingChannel = 0;

// FIX SERIAL-03: Canal cacheado — lido UMA vez no setupESPNow(), sem NVS no loop.
// Antes: NVS era aberto a cada heartbeat (processESPNowPairing) e beacon (reconexao),
// causando contencao com WiFi task e bloqueio do loop.
uint8_t cachedChannel = ESPNOW_CHANNEL;

void onDataSent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    // Vazio — Sensor B nao usa status de envio
}

/**
 * Callback de recepcao ESP-NOW.
 * RODA NA TASK WIFI — restricoes de ISR:
 * - NAO usar Serial (mutex → deadlock)
 * - NAO usar display/SPI (contencao → crash)
 * - NAO chamar esp_now_add_peer (alloc → risco)
 * - SO copiar dados e setar flags volateis
 */
void onDataRecv(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < 1) {
        return;
    }

    uint8_t type = data[0];

    if (type == MSG_HEARTBEAT && len >= static_cast<int>(sizeof(HeartbeatPacket))) {
        // FIX C08: Spinlock protege copia do MAC (6 bytes nao atomico)
        portENTER_CRITICAL_ISR(&macMux);
        memcpy(sensorA_mac, mac, 6);
        portEXIT_CRITICAL_ISR(&macMux);

        isConnected = true;
        lastHeartbeat = millis();
        displayDirty = true;

        // FIX C07 + A↔B #5: Peer add diferido com spinlock
        portENTER_CRITICAL_ISR(&pairMux);
        memcpy(pendingMac, mac, 6);
        peerAddNeeded = true;
        portEXIT_CRITICAL_ISR(&pairMux);
    }
    else if (type == MSG_SET_CHANNEL && len >= 2) {
        // FIX BUG-08: Troca de canal diferida — so seta flag no callback.
        // NVS e restart sao processados no loop() via processChannelChange().
        uint8_t newCh = data[1];
        if (newCh >= 1 && newCh <= 13) {
            pendingChannel = newCh;
            channelChangeNeeded = true;
        }
    }
    else if (type == MSG_CATEGORY && len >= static_cast<int>(sizeof(CategoryPacket))) {
        const CategoryPacket* catPkt = reinterpret_cast<const CategoryPacket*>(data);

        // FIX C07: Validacao de bounds antes de aceitar categoria da rede
        if (catPkt->category < CAT_COUNT) {
            currentCategory = catPkt->category;
        }
        displayDirty = true;
        // FIX C07: Removido updateDisplay() — displayDirty ja faz o redraw no loop()
    }
}

// --- Inicializacao ---

bool setupESPNow() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // FIX INSTALL-10: Aguardar WiFi STA inicializar completamente.
    // Sem esse delay, esp_wifi_set_channel() pode ser ignorado e os
    // dispositivos ficam em canais diferentes (nunca se encontram).
    // Referencia: exemplo oficial Espressif ESP_NOW_Broadcast_Master.ino
    // usa while(!WiFi.STA.started()) { delay(100); }
    delay(200);

    // FIX SERIAL-03: Ler canal do NVS UMA vez e cachear.
    {
        Preferences prefs;
        if (prefs.begin("gestuum_ch", true)) {
            cachedChannel = prefs.getUChar("channel", ESPNOW_CHANNEL);
            prefs.end();
        }
        if (cachedChannel < 1 || cachedChannel > 13) cachedChannel = ESPNOW_CHANNEL;
    }
    esp_wifi_set_channel(cachedChannel, WIFI_SECOND_CHAN_NONE);

    // FIX INSTALL-10: Verificar canal real aplicado
    uint8_t primary_ch = 0;
    wifi_second_chan_t second_ch;
    esp_wifi_get_channel(&primary_ch, &second_ch);
    Serial.printf("[ESPNOW] Canal configurado: %d | Canal real: %d\n", cachedChannel, primary_ch);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESPNOW] esp_now_init FAILED");
        return false;
    }

    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);

    // FIX INSTALL-10: Broadcast peer com FF:FF:FF:FF:FF:FF explicito
    // e usando canal real (variavel 'channel') em vez de constante ESPNOW_CHANNEL.
    // Antes usava sensorA_mac (que no boot e FF:FF... mas semanticamente errado).
    uint8_t broadcast_addr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_peer_info_t broadcastPeer;
    memset(&broadcastPeer, 0, sizeof(broadcastPeer));
    memcpy(broadcastPeer.peer_addr, broadcast_addr, 6);
    broadcastPeer.channel = cachedChannel;
    broadcastPeer.encrypt = false;

    if (esp_now_add_peer(&broadcastPeer) != ESP_OK) {
        Serial.println("[ESPNOW] add broadcast peer FAILED");
        return false;
    }

    Serial.println("[ESPNOW] initialized OK");
    return true;
}

// --- Envio ---

void sendHeartbeat() {
    if (!isConnected) {
        return;
    }

    HeartbeatPacket pkt;
    pkt.type = MSG_HEARTBEAT;
    pkt.sender = 1;  // 1 = Sensor B
    int32_t battRaw = StickCP2.Power.getBatteryLevel();
    if (battRaw < 0) battRaw = 0;
    if (battRaw > 100) battRaw = 100;
    pkt.battery = static_cast<uint8_t>(battRaw);

    // FIX C08: Copia MAC sob spinlock (6 bytes = nao atomico)
    uint8_t mac[6];
    portENTER_CRITICAL(&macMux);
    memcpy(mac, sensorA_mac, 6);
    portEXIT_CRITICAL(&macMux);

    esp_now_send(mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(HeartbeatPacket));
}

void sendIMUData(const IMUPacket& packet) {
    if (isConnected) {
        // FIX C08: Copia MAC sob spinlock
        uint8_t mac[6];
        portENTER_CRITICAL(&macMux);
        memcpy(mac, sensorA_mac, 6);
        portEXIT_CRITICAL(&macMux);

        esp_now_send(mac, reinterpret_cast<const uint8_t*>(&packet), sizeof(IMUPacket));
    } else {
        unsigned long now = millis();
        if (now - lastDisconnectedWarning >= 1000) {
            lastDisconnectedWarning = now;
            Serial.println("[ESPNOW] Not connected — IMU data not sent");
        }
    }
}

/**
 * FIX C07: Processa peer add diferido.
 * DEVE ser chamado no loop() do main.cpp.
 *
 * O callback nao pode chamar esp_now_add_peer() porque
 * roda na task WiFi e a funcao faz alocacao de memoria.
 */
void processESPNowPairing() {
    if (peerAddNeeded) {
        // FIX A↔B #5: Copia MAC sob spinlock
        uint8_t mac[6];
        portENTER_CRITICAL(&pairMux);
        memcpy(mac, pendingMac, 6);
        peerAddNeeded = false;
        portEXIT_CRITICAL(&pairMux);

        esp_now_peer_info_t peerInfo;
        memset(&peerInfo, 0, sizeof(peerInfo));
        memcpy(peerInfo.peer_addr, mac, 6);
        // FIX BUG-04 + SERIAL-03: Usar canal cacheado (sem NVS no loop)
        peerInfo.channel = cachedChannel;
        peerInfo.encrypt = false;

        if (!esp_now_is_peer_exist(mac)) {
            esp_err_t err = esp_now_add_peer(&peerInfo);
            if (err == ESP_OK) {
                Serial.println("[ESPNOW] Sensor A peer added");
            } else {
                Serial.printf("[ESPNOW] add peer failed: %d\n", err);
            }
        }
    }
}

// FIX BUG-08: Processar troca de canal no contexto do loop (seguro para NVS).
void processChannelChange() {
    if (channelChangeNeeded) {
        channelChangeNeeded = false;
        uint8_t newCh = pendingChannel;
        Serial.printf("[ESPNOW] Trocando canal para %d (diferido do callback)\n", newCh);
        Preferences prefs;
        if (prefs.begin("gestuum_ch", false)) {
            prefs.putUChar("channel", newCh);
            prefs.end();
        }
        delay(100);  // Tempo para flush serial
        ESP.restart();
    }
}
