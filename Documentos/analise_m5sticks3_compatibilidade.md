# Analise de compatibilidade — M5StickS3 vs Gestuum atual

> Data: 2026-04-29
> Documento de referencia: `docs/documentacao/novo_m5.pdf`
> Pergunta: dificuldade de comprar M5StickC Plus2; o StickS3 substitui sem mexer no codigo?

## Veredito

**NAO da para usar sem alterar o codigo.** O StickS3 e um produto novo, nao uma revisao do Plus2. Mudam o chip, a biblioteca, o IMU, o pinout, o codec de audio e — o mais grave para o Gestuum — **acaba o Bluetooth Classic / A2DP**.

Para ainda usar StickS3 seria uma **portabilidade** de firmware, nao um simples re-upload.

## Comparacao direta

| Item | M5StickC Plus2 (atual) | StickS3 (novo) | Impacto |
|------|------------------------|----------------|---------|
| SoC | ESP32-PICO-V3-02 (LX6) | ESP32-S3-PICO-1-N8R8 (LX7) | board PlatformIO diferente |
| Flash / PSRAM | 8MB / 2MB | 8MB / 8MB | OK (mais PSRAM) |
| **Bluetooth Classic / A2DP** | **Sim** | **NAO (so BLE)** | **Quebra o audio BT do Gestuum** |
| Wi-Fi | 2.4 GHz | 2.4 GHz | OK |
| ESP-NOW | Sim | Sim | OK (canal 13 funciona) |
| IMU | MPU-6886 | BMI270 | Driver diferente; gestos precisam re-calibrar |
| Display | ST7789V 135x240 | ST7789P3 135x240 | Mesmo tamanho, driver levemente diferente |
| Audio (saida) | Buzzer passivo (GPIO2) + HAT-SPK2 (I2S) | ES8311 codec + AW8737 + speaker 8Ω@1W (built-in) | Toda a camada de audio muda |
| Microfone | SPM1423 PDM | MEMS via ES8311 I2S | Pipeline de captura muda |
| Biblioteca PIO | `m5stack/M5StickCPlus2@^1.0.2` | `M5Unified` + `M5PM1` | Toda chamada `M5.*` precisa revisar |
| Pinout | GPIO37/39 botoes, GPIO32/33 Grove, etc | G11/G12 botoes, G9/G10 Grove, G46/G42 IR | Reescrever `constants.h` |
| Bateria | 200 mAh | 250 mAh | OK (melhor) |

## Por que o BT A2DP e o problema critico

O Gestuum atual usa Bluetooth A2DP para mandar audio para uma caixa BT (JBL Clip 5 nos testes recentes). A2DP e um perfil de **Bluetooth Classic** — e o ESP32-S3 **nao tem** Bluetooth Classic, so BLE. Isso e limitacao da Espressif, nao da M5Stack: nao se resolve com biblioteca.

Se for usar StickS3, ou:
- abandona caixa BT externa e usa o speaker integrado (1W, mono);
- ou troca para streaming via Wi-Fi (ex.: PCM/UDP para um receptor) — exige escrever protocolo novo dos dois lados.

## O que mudaria no codigo (alto nivel)

1. **`platformio.ini`** dos tres firmwares (sensor_a/b, atoms3_led nao muda) — board, lib_deps, build_flags, particao novos.
2. **`shared/constants.h`** — todos os pinos.
3. **Driver IMU** — M5Unified abstrai, mas escalas/offsets do BMI270 ≠ MPU-6886. Re-calibrar grid 11x11x11 e DTW.
4. **VoiceManager / camada de audio** — sair de `M5.Speaker.playRaw()` (HAT-SPK2 via I2S generico) para ES8311 via M5Unified; e decidir o que fazer com o BT A2DP.
5. **SPIFFS** — particao precisa ser refeita (tabela de particao do S3 e diferente).
6. **Re-upload de WAVs e gestos** apos cada flash inicial (uploadfs apaga tudo, regra ja conhecida).

## Recomendacao

**Curto prazo (demo do pai / S1-S2-S3):** continuar caçando Plus2. E o caminho de menor risco antes da Science Fair. O firmware atual (53 gestos, BT A2DP, VoiceManager) esta validado nele.

**Medio prazo (MVP COREE 2026):** se o Plus2 sumir do mercado, o StickS3 e candidato viavel — mas conte como **sprint de portabilidade dedicada**, nao como troca direta. Ordem sugerida:
1. Comprar 1 unidade so para testes de bancada (nao mexer no kit funcional).
2. Validar IMU (BMI270 + grid 11) com 1 gesto antes de migrar todos.
3. Decidir audio: speaker integrado vs Wi-Fi streaming.
4. So depois replicar para o Sensor B.

**Onde comprar Plus2 (ideias para destravar):**
- M5Stack oficial (m5stack.com) — manda do Shenzhen, demora mas tem estoque.
- Distribuidores: DigiKey, Mouser, Aliexpress (M5Stack official store).
- No Brasil: Curto Circuito, FilipeFlop — checar se ainda listam o Plus2 (nao o Plus v1).

## Resumo em uma frase

StickS3 e um upgrade legitimo de hardware, mas e **outro produto** — adotar exige portar o firmware e perder o BT Classic, entao **nao serve como peca de reposicao** para o estado atual do Gestuum.
