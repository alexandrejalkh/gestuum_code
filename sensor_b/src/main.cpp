/**
 * GESTUUM — Sensor B Main Entry Point
 * Bloco: 2.1 — Sensor B firmware
 * Responsibility: Initialize hardware, run 50Hz IMU read/send loop,
 *                 monitor connection timeout, drive display.
 *
 * Fix H4: Removed delay(1) — pure non-blocking timing via millis().
 * Fix L3: Reconnection logic — broadcast heartbeat request after 10s disconnect.
 * Fix: Battery clamped 0-100 before display.
 */

#include <M5StickCPlus2.h>
#include <esp_now.h>
#include <Preferences.h>
#include "config.h"
#include "constants.h"
#include "protocol.h"
#include "imu_reader.h"
#include "espnow_comm.h"

// === Estado global (declarado extern volatile em config.h) ===
// FIX #4: volatile nas definicoes — antes so estava nas declaracoes (UB).
// Sem volatile, compilador pode cachear em registrador e nunca ver updates do callback.
uint8_t sensorA_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
volatile bool isConnected = false;
volatile unsigned long lastHeartbeat = 0;
volatile uint8_t currentCategory = CAT_GERAL;
volatile bool displayDirty = true;

// === Local state ===
static IMUReader imu;
static unsigned long lastSend = 0;
static uint16_t imuSeqCounter = 0;  // FIX A↔B #7: sequence number para detectar packet loss
static unsigned long lastReconnectAttempt = 0;
static unsigned long lastHeartbeatSent = 0;
static bool wasConnected = false;  // Track previous connection state for display updates

// Reconnection: time after disconnect before sending broadcast heartbeat request
static const unsigned long RECONNECT_DELAY_MS = 3000;
// Reconnection: interval between broadcast heartbeat requests
static const unsigned long RECONNECT_INTERVAL_MS = 2000;

// === Category name/color tables ===
static const char* const CATEGORY_NAMES[CAT_COUNT] = {
    "GERAL",
    "EMERGENCIA",
    "CASA",
    "TRABALHO",
    "SOCIAL"
};

// Display colors in RGB565 format for M5GFX
static const uint16_t CATEGORY_COLORS_565[CAT_COUNT] = {
    0x001F,  // Blue    (GERAL)
    0xF800,  // Red     (EMERGENCIA)
    0x07E0,  // Green   (CASA)
    0xFFE0,  // Yellow  (TRABALHO)
    0x801F   // Purple  (SOCIAL)
};

// === Display ===

void updateDisplay(uint8_t category) {
    auto& disp = StickCP2.Display;
    (void)category;  // Categorias removidas do MVP

    disp.fillScreen(TFT_BLACK);

    // GESTUUM com cor da logo (mint green #7BC09A ≈ 0x7E54)
    uint16_t logoColor = 0x7E54;
    disp.setTextSize(2);
    disp.setTextColor(logoColor, TFT_BLACK);
    disp.setCursor(10, 8);
    disp.print("GESTUUM");

    // Linha separadora
    disp.drawFastHLine(0, 30, 240, 0x4208);  // Cinza escuro

    // Connection status
    disp.setTextSize(2);
    disp.setCursor(10, 45);
    if (isConnected) {
        disp.setTextColor(TFT_GREEN, TFT_BLACK);
        disp.print("Conectado");
    } else {
        disp.setTextColor(TFT_RED, TFT_BLACK);
        disp.print("Aguardando...");
    }

    // Battery level
    int32_t batteryLevel = StickCP2.Power.getBatteryLevel();
    if (batteryLevel < 0) batteryLevel = 0;
    if (batteryLevel > 100) batteryLevel = 100;

    // Bateria no canto superior direito
    disp.setTextSize(1);
    disp.setTextColor(batteryLevel > 20 ? TFT_WHITE : TFT_RED, TFT_BLACK);
    char batStr[8];
    snprintf(batStr, sizeof(batStr), "%d%%", batteryLevel);
    disp.setTextDatum(TR_DATUM);
    disp.drawString(batStr, 235, 4);
    disp.setTextDatum(TL_DATUM);  // Resetar

    // Sensor B label
    disp.setTextSize(1);
    disp.setTextColor(0x4208, TFT_BLACK);
    disp.setCursor(10, 80);
    disp.print("Sensor B | Mao Direita");

    displayDirty = false;
}

// === Setup ===

void setup() {
    // Initialize M5StickC Plus2 (also initializes IMU, Power, Display)
    // FIX INSTALL-12: Config explicita + delay para garantir botoes inicializados
    auto cfg = M5.config();
    cfg.clear_display = true;
    StickCP2.begin(cfg);

    // Aguardar inicializacao completa do hardware
    delay(100);
    M5.update();  // FIX INSTALL-11: M5 em vez de StickCP2

    // Horizontal screen orientation
    StickCP2.Display.setRotation(1);

    // Initialize IMU reader (filter state reset)
    imu.begin();

    // Initialize ESP-NOW communication
    if (!setupESPNow()) {
        StickCP2.Display.fillScreen(TFT_BLACK);
        StickCP2.Display.setTextColor(TFT_RED, TFT_BLACK);
        StickCP2.Display.setTextSize(2);
        StickCP2.Display.setCursor(10, 30);
        StickCP2.Display.print("ESP-NOW ERRO!");
        while (true) {
            delay(1000);
        }
    }

    // Draw initial display
    updateDisplay(CAT_GERAL);

    // Serial for debug logging
    Serial.begin(115200);
    Serial.println("[SENSOR_B] Initialized");
}

// === Loop ===

void loop() {
    // FIX INSTALL-11: M5.update() em vez de StickCP2.update()
    // Static Initialization Order Fiasco: StickCP2.BtnA/BtnB sao referencias
    // que podem apontar para memoria nao inicializada.
    M5.update();

    // FIX CRITICAL #1: Processar pairing diferido do callback ESP-NOW.
    // Sem esta chamada, Sensor A nunca era adicionado como peer unicast
    // e TODOS os envios de IMU/heartbeat falhavam com ESP_ERR_ESPNOW_NOT_FOUND.
    processESPNowPairing();

    // FIX BUG-08: Processar troca de canal diferida (NVS + restart seguro)
    processChannelChange();

    unsigned long now = millis();

    // Read IMU and send data at 50Hz (every 20ms)
    if (now - lastSend >= IMU_SAMPLE_PERIOD_MS) {
        lastSend = now;

        imu.update();

        // Build IMU packet
        IMUPacket packet;
        packet.type = MSG_IMU_DATA;
        packet.timestamp = now;
        packet.seq = imuSeqCounter++;  // FIX A↔B #7: wrapa automaticamente em 65535
        packet.ax = imu.getScaledAccelX();
        packet.ay = imu.getScaledAccelY();
        packet.az = imu.getScaledAccelZ();
        packet.gx = imu.getScaledGyroX();
        packet.gy = imu.getScaledGyroY();
        packet.gz = imu.getScaledGyroZ();

        // Clamp battery to 0-100 for the packet as well
        int32_t battRaw = StickCP2.Power.getBatteryLevel();
        if (battRaw < 0) battRaw = 0;
        if (battRaw > 100) battRaw = 100;
        packet.battery = static_cast<uint8_t>(battRaw);

        sendIMUData(packet);
    }

    // Send heartbeat back to Sensor A every HEARTBEAT_INTERVAL_MS (1000ms)
    if (now - lastHeartbeatSent >= HEARTBEAT_INTERVAL_MS) {
        lastHeartbeatSent = now;
        sendHeartbeat();
    }

    // Check connection timeout
    if (isConnected && (now - lastHeartbeat > CONNECTION_TIMEOUT_MS)) {
        isConnected = false;
        displayDirty = true;
        Serial.println("[SENSOR_B] Connection lost — heartbeat timeout");
    }

    // Fix L3: Reconnection logic — if disconnected for >10s, broadcast heartbeat request
    if (!isConnected) {
        unsigned long timeSinceLastHeartbeat = now - lastHeartbeat;
        if (timeSinceLastHeartbeat > RECONNECT_DELAY_MS) {
            if (now - lastReconnectAttempt >= RECONNECT_INTERVAL_MS) {
                lastReconnectAttempt = now;

                // Send a lightweight IMU packet to broadcast address to trigger
                // Sensor A to discover us and send its heartbeat
                uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

                // Ensure broadcast peer exists
                if (!esp_now_is_peer_exist(broadcastAddr)) {
                    esp_now_peer_info_t broadcastPeer;
                    memset(&broadcastPeer, 0, sizeof(broadcastPeer));
                    memcpy(broadcastPeer.peer_addr, broadcastAddr, 6);
                    // FIX BUG-04 + SERIAL-03: Canal cacheado (sem NVS no loop)
                    broadcastPeer.channel = cachedChannel;
                    broadcastPeer.encrypt = false;
                    esp_now_add_peer(&broadcastPeer);
                }

                // FIX #10: Beacon de reconexao usa HeartbeatPacket (3 bytes)
                // Antes: enviava IMUPacket zerado (18 bytes de lixo que alimentava
                // o gesture engine com dados falsos). HeartbeatPacket e semanticamente
                // correto e menor.
                HeartbeatPacket beacon;
                beacon.type = MSG_HEARTBEAT;
                beacon.sender = 1;  // 1 = Sensor B
                beacon.battery = 0;
                esp_now_send(broadcastAddr, reinterpret_cast<const uint8_t*>(&beacon), sizeof(HeartbeatPacket));

                Serial.println("[SENSOR_B] Reconnect: broadcast beacon sent");
            }
        }
    }

    // Track connection state changes for display updates
    if (wasConnected != isConnected) {
        wasConnected = isConnected;
        displayDirty = true;
    }

    // Redraw display only when state changed
    if (displayDirty) {
        updateDisplay(currentCategory);
    }

    // FIX A↔B #12: Emergencia pelo Sensor B — hold BtnA 2 segundos.
    // FIX INSTALL-11: Usar M5.BtnA em vez de StickCP2.BtnA (SIOF fix)
    if (M5.BtnA.wasHold()) {
        // Broadcast emergencia direto — nao precisa passar pelo Sensor A
        EmergencyPacket pkt;
        pkt.type = MSG_EMERGENCY;
        pkt.level = 3;  // Critico
        pkt.gesture_id = 0;

        uint8_t broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        // Envia 3x para redundancia (broadcast sem ACK)
        for (int i = 0; i < 3; i++) {
            esp_now_send(broadcastAddr, reinterpret_cast<const uint8_t*>(&pkt), sizeof(EmergencyPacket));
            delay(50);
        }

        // Tambem envia para Sensor A (se conectado) para ativar SOS no audio
        if (isConnected) {
            uint8_t mac[6];
            portENTER_CRITICAL(&macMux);
            memcpy(mac, sensorA_mac, 6);
            portEXIT_CRITICAL(&macMux);
            esp_now_send(mac, reinterpret_cast<const uint8_t*>(&pkt), sizeof(EmergencyPacket));
        }

        // Feedback visual no Sensor B
        StickCP2.Display.fillScreen(TFT_RED);
        StickCP2.Display.setTextColor(TFT_WHITE);
        StickCP2.Display.setTextDatum(MC_DATUM);
        StickCP2.Display.setTextSize(3);
        StickCP2.Display.drawString("SOS", 120, 67);
        displayDirty = true;

        Serial.println("[SENSOR_B] EMERGENCY triggered via BtnA hold");

        // Espera soltar o botao para nao re-triggerar
        while (M5.BtnA.isPressed()) {
            M5.update();
            delay(50);
        }
    }
}
