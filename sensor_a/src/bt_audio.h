/**
 * GESTUUM — Bluetooth A2DP Audio Source
 * Responsibility: Streaming de audio PCM para caixa de som / fone Bluetooth.
 *
 * O ESP32 suporta A2DP Source nativamente (Bluetooth Classic).
 * Este modulo gerencia: descoberta, pareamento, conexao e envio de audio.
 *
 * Uso:
 *   BTAudio btAudio;
 *   btAudio.begin("GESTUUM");     // Inicia BT com nome do dispositivo
 *   btAudio.connectToSaved();     // Tenta conectar ao ultimo speaker salvo
 *   btAudio.playPCM(data, len, sampleRate);  // Envia audio
 *
 * Coexistencia: BT Classic (A2DP) roda junto com BLE (config) no ESP32.
 * ESP-NOW usa WiFi que coexiste com BT.
 */

#ifndef GESTUUM_BT_AUDIO_H
#define GESTUUM_BT_AUDIO_H

#include <stdint.h>
#include <stddef.h>
#include <esp_a2dp_api.h>
#include <esp_gap_bt_api.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class BTAudio {
public:
    BTAudio();
    ~BTAudio();

    /**
     * Inicializa o Bluetooth Classic e registra como A2DP Source.
     * @param deviceName Nome do dispositivo BT (visivel na caixa de som).
     * @return true se inicializou com sucesso.
     */
    bool begin(const char* deviceName = "GESTUUM");

    /**
     * Tenta conectar ao ultimo speaker BT salvo no NVS.
     * Se nao tiver speaker salvo, nao faz nada (fallback para HAT-SPK2).
     */
    void connectToSaved();

    /**
     * Inicia descoberta de dispositivos BT e conecta ao primeiro
     * A2DP Sink (caixa de som) encontrado.
     * Bloqueante por ate timeoutMs.
     * @param timeoutMs Tempo maximo de busca (default 10s).
     * @return true se conectou.
     */
    bool discoverAndConnect(uint32_t timeoutMs = 10000);

    /**
     * Envia dados PCM para o speaker BT conectado.
     * @param data Samples PCM 16-bit signed mono.
     * @param numSamples Numero de samples.
     * @param sampleRate Taxa de amostragem (ex: 48000).
     * @return true se os dados foram enfileirados para envio.
     */
    bool playPCM(const int16_t* data, size_t numSamples, uint32_t sampleRate);

    /** @return true se ha um speaker BT conectado. */
    bool isConnected() const;

    /** @return true se ainda ha dados sendo transmitidos. */
    bool isPlaying() const;

    /** Para o streaming de audio imediatamente. */
    void stop();

    /** Desconecta do speaker BT. */
    void disconnect();

    /**
     * Salva o MAC do speaker conectado no NVS para reconexao automatica.
     */
    void saveCurrentSpeaker();

    /**
     * Deve ser chamado no loop() para processar eventos BT.
     */
    void update();

private:
    bool _initialized;
    volatile bool _connected;       // FIX AUDIT-02: volatile — escrito pelo callback BT, lido pelo loop
    volatile bool _playing;         // FIX AUDIT-02: volatile — escrito pelo callback BT, lido pelo loop
    uint8_t _peerAddr[6];       // MAC do speaker conectado
    bool _hasSavedSpeaker;      // Tem speaker salvo no NVS?

    // FIX AUDIT-01: Mutex protege acesso concorrente entre playPCM() (loop)
    // e a2dpDataCallback() (task BT). Sem mutex, free() no loop + read() no
    // callback = use-after-free → crash intermitente.
    SemaphoreHandle_t _pcmMutex;

    // Buffer de audio para A2DP (ponteiro para dados na PSRAM)
    // playPCM() aloca copia na PSRAM, callback le no ritmo do BT
    int16_t* _pcmData;              // Ponteiro para samples (alocado em PSRAM)
    volatile size_t _pcmTotalSamples;  // Total de samples no buffer
    volatile size_t _pcmReadPos;       // Posicao de leitura atual do callback

    // Callback A2DP que o BT stack chama quando precisa de dados
    static int32_t a2dpDataCallback(uint8_t* data, int32_t len);

    // Callbacks de eventos A2DP e GAP (descoberta)
    static void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* param);
    static void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* param);

    // Helpers NVS
    bool loadSavedSpeaker();
    void saveSpeaker(const uint8_t* addr);
};

#endif // GESTUUM_BT_AUDIO_H
