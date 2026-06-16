/**
 * ============================================================
 * GESTUUM — Definicao de constantes globais
 * ============================================================
 * FIX M03: LEVEL_CONFIGS declarado como extern no header,
 * definido aqui UMA vez para evitar duplicacao em cada
 * translation unit que inclui constants.h.
 * ============================================================
 */

#include "constants.h"

// Configuracoes dos 3 niveis de acessibilidade:
// - LIMITED:  gestos curtos, matching tolerante, ativacao leve (paralisia, PC severa)
// - STANDARD: equilibrado, uso geral (padrao)
// - ADVANCED: gestos complexos, matching rigoroso, ativacao firme (LIBRAS, mobilidade total)
//
// Campos: minRecordingMs, gestureTimeoutMs, stableDurationMs,
//         dtwThresholdMultiplier, doubleTapThreshold, minTrajectoryLen
//
// UX FIX: Deteccao precoce mais rapida.
// FIX ALT-14: Comentario atualizado para refletir valores reais.
// STANDARD: minRecording=2000ms, stable=500ms (calibrado INSTALL-16)
// No dia a dia (rua, escola, hospital), esperar 3s parado e inviavel.
// A deteccao precoce agora encerra o gesto assim que a mao para,
// sem forcar espera minima desnecessaria.
const LevelConfig LEVEL_CONFIGS[LEVEL_COUNT] = {
    // FIX INSTALL-15: Calibrado com testes reais nos dispositivos.
    // Threshold 1.8g era muito sensivel — ativava ao pegar no sensor.
    // 2.2g requer tap intencional, nao ativa ao pegar/mover normalmente.
    // minTrajectoryLen reduzido: gestos reais geram 2-4 pontos na grid 7x7x7.
    // FIX INSTALL-16: minRecordingMs = tempo MINIMO que o gesto deve durar.
    // Com ACCEL_RANGE=2.0g + countdown, micro-movimentos geram dados rapido.
    // 2000ms garante que o usuario tem tempo de fazer o gesto completo.
    // stableDurationMs = tempo que a mao deve ficar parada para encerrar.
    { 1000, 4000, 500, 1.4f, 1.8f, 2 },  // LEVEL_LIMITED  (minRec: 1s, timeout: 4s, stable: 500ms)
    { 2000, 5000, 500, 1.0f, 2.2f, 3 },  // LEVEL_STANDARD (minRec: 2s, timeout: 5s, stable: 500ms)
    { 2500, 7000, 300, 0.7f, 3.0f, 5 },  // LEVEL_ADVANCED (minRec: 2.5s, timeout: 7s, stable: 300ms)
};
