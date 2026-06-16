/**
 * GESTUUM — Menu UI Local v2 (Caminho C / Sprint UX-v2)
 *
 * v2 (2026-05-03): refatorado pra "1 item por tela" (carrossel) com
 * paleta de cores por item. Sub-menu Treinar/Apagar agora pede primeiro
 * a MAO (Esquerda/Direita/Solo) antes de mostrar lista filtrada.
 *
 * Mapeamento de botoes (NOVO):
 *   1x BtnB (pequeno lateral) -> proximo item (carrossel circular)
 *   2x BtnB                   -> item anterior (carrossel circular)
 *   3x BtnB                   -> abrir/sair menu (toggle)
 *   1x BtnA (grande frontal)  -> confirmar / entrar
 *   Hold BtnB 1s              -> voltar 1 nivel (back-to-parent)
 *
 * Decisao de design: cores RGB565 por item dao discriminacao visual
 * melhor que glyph ASCII em display 240x135 pequeno. Texto grande
 * (size 3) caixa alta no centro. Header colorido. Footer instrucoes.
 */

#ifndef MENU_UI_H
#define MENU_UI_H

#include <Arduino.h>
#include <stddef.h>

// Niveis hierarquicos do menu
enum MenuLevel : uint8_t {
    MENU_LEVEL_NONE = 0,     // fora do menu
    MENU_LEVEL_MAIN = 1,     // carrossel main (1 item por tela)
    MENU_LEVEL_SUB = 2,      // sub-menu (hand-picker ou gesture-by-hand)
    MENU_LEVEL_ACTION = 3    // tela de placeholder/confirm/about/silent
};

// Itens do menu principal (ordem do carrossel)
enum MenuMainItem : uint8_t {
    MAIN_TREINAR = 0,
    MAIN_APAGAR,
    MAIN_BLUETOOTH,
    MAIN_CANAL,
    MAIN_SILENCIOSO,
    MAIN_SOBRE,
    MAIN_COUNT
};

// Tipo de sub-menu ativo (so um por vez)
enum SubmenuKind : uint8_t {
    SUB_NONE = 0,
    SUB_TRAIN_HAND_PICK = 1,    // escolha mao (Esq/Dir/Solo) p/ TREINAR
    SUB_TRAIN_GESTURE = 2,       // carrossel de gestos da mao escolhida (treinar)
    SUB_DELETE_HAND_PICK = 3,    // idem pra APAGAR
    SUB_DELETE_GESTURE = 4
};

// Filtro de mao (qual subset de gestos mostrar).
// Prefixo HF_ pra nao conflitar com HandDominance em gesture_engine.h.
enum HandFilter : uint8_t {
    HF_LEFT = 0,    // contexto (isContext == true)
    HF_RIGHT = 1,   // objeto (!isContext && !isSolo)
    HF_SOLO = 2,    // independente (isSolo == true)
    HF_COUNT = 3
};

// Tipo de tela ACTION ativa
enum ActionKind : uint8_t {
    ACTION_PLACEHOLDER = 0,
    ACTION_DELETE_CONFIRM = 1,
    ACTION_ABOUT = 2,
    ACTION_SILENT = 3
};

class MenuUI {
public:
    void begin();
    void enter();
    void exit();
    bool isOpen() const { return _level != MENU_LEVEL_NONE; }

    // === Navegacao (chamadas pelo main.cpp loop) ===

    // 1xB = proximo item no nivel/carrossel atual
    void onNavigateNext();

    // 2xB = item anterior no carrossel atual (NOVO em v2)
    void onNavigatePrev();

    // 1xA = confirma/entra
    void onConfirm();

    // Hold B 1s = volta 1 nivel (NOVO em v2 — substitui o 2xB-back antigo)
    void onBackToParent();

    // Render display (idempotente — so redesenha se _dirty)
    void render();

    // === Pendentes one-shot consumidos pelo main.cpp ===

    bool consumePendingTrain(char* idStrOut, size_t idStrSz,
                             char* nameOut, size_t nameSz);
    bool consumePendingDelete(char* idStrOut, size_t idStrSz,
                              char* nameOut, size_t nameSz);
    bool consumePendingBluetoothActivate();
    bool consumePendingSilentToggle();

    // Re-abrir menu apos training/delete (UX continua)
    void reopenAtTrainGesture();   // volta no SUB_TRAIN_GESTURE com mesmo handFilter
    void reopenAtDeleteGesture();  // idem pra delete
    void reopenAtSilentScreen();

private:
    MenuLevel _level = MENU_LEVEL_NONE;
    uint8_t _mainCursor = 0;
    SubmenuKind _subKind = SUB_NONE;

    // Hand picker (cursor em qual mao escolher)
    uint8_t _handPickerCursor = 0;  // 0=Esq, 1=Dir, 2=Solo

    // Filtro de mao ativo (definido apos confirmar no hand picker)
    HandFilter _handFilter = HF_LEFT;

    // Cursor de gesto dentro do filtro de mao (lista filtrada)
    int _gestureCursor = 0;

    // Lista de indices de gesto que batem no filtro atual.
    // Recalculada toda vez que entra em SUB_*_GESTURE ou apos hot-reload.
    static const int MAX_FILTERED = 80;
    int _filteredIndices[MAX_FILTERED];
    int _filteredCount = 0;

    // Pendentes one-shot
    bool _pendingTrainValid = false;
    char _pendingTrainIdStr[8];
    char _pendingTrainName[32];

    bool _pendingDeleteValid = false;
    char _pendingDeleteIdStr[8];
    char _pendingDeleteName[32];

    bool _pendingBluetoothActivate = false;
    bool _pendingSilentToggle = false;

    // ACTION state
    ActionKind _actionKind = ACTION_PLACEHOLDER;
    char _deleteConfirmIdStr[8];
    char _deleteConfirmName[32];

    bool _dirty = true;
    unsigned long _enterMs = 0;

    // === Helpers internos ===

    void enterTrainHandPicker();
    void enterDeleteHandPicker();
    void enterGestureCarousel(SubmenuKind kind, HandFilter hand);
    void rebuildFilteredList(HandFilter hand);

    // Renders (1 tela por funcao)
    void renderMainCarousel();
    void renderHandPicker(const char* title);
    void renderGestureCarousel(const char* title, bool deleteMode);
    void renderActionPlaceholder(const char* itemName);
    void renderActionDeleteConfirm();
    void renderActionAbout();
    void renderActionSilent();

    // Util de UI
    void drawHeader(uint16_t color, const char* title, const char* counter);
    void drawFooter(const char* legend);
    void drawCenteredTitle(const char* line1, const char* line2);
};

extern MenuUI menuUI;

#endif // MENU_UI_H
