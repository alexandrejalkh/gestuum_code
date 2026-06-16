/**
 * GESTUUM — Menu UI Local v2 (Caminho C / Sprint UX-v2)
 *
 * Implementacao "1 item por tela" com paleta de cores por item.
 * Substitui v1 que era lista vertical apertada (tela 240x135 nao
 * comportava bem 6 itens visiveis).
 *
 * Decisoes de design (2026-05-03 com dono):
 * - Sem glyphs ASCII: discriminacao visual via cor de header
 * - Texto centrado size 3 (CAIXA ALTA) + subtitulo size 2
 * - Sub-menu Treinar/Apagar pede mao (Esq/Dir/Solo) antes de listar gestos
 * - Hold B 1s = voltar nivel (substitui o 2xB-back antigo, agora 2xB = prev)
 * - Carrossel circular (wrap em ambos lados)
 */

#include "menu_ui.h"
#include "config.h"
#include "gesture_engine.h"
#include "audio_player.h"
#include "espnow_comm.h"
#include <M5StickCPlus2.h>
#include <WiFi.h>
#include <esp_system.h>

extern GestureEngine gestureEngine;
extern AudioPlayer audioPlayer;

MenuUI menuUI;

// ============================================================================
// Paleta de cores RGB565 — discriminacao visual por item/categoria
// ============================================================================

// Background geral (azul escuro)
static constexpr uint16_t COLOR_BG          = 0x18C3;
static constexpr uint16_t COLOR_TEXT        = 0xFFFF;  // branco
static constexpr uint16_t COLOR_TEXT_DIM    = 0xC618;  // cinza claro
static constexpr uint16_t COLOR_FOOTER      = 0x4208;  // cinza escuro
static constexpr uint16_t COLOR_TEXT_BLACK  = 0x0000;

// Cores por item MAIN (header colorido)
static constexpr uint16_t COL_TREINAR    = 0x07E0;  // verde — criar/positivo
static constexpr uint16_t COL_APAGAR     = 0xC000;  // vermelho — destrutivo
static constexpr uint16_t COL_BLUETOOTH  = 0x041F;  // azul — BT classico
static constexpr uint16_t COL_CANAL      = 0x630C;  // cinza — postergado v2
static constexpr uint16_t COL_SILENCIOSO = 0x780F;  // roxo — config
static constexpr uint16_t COL_SOBRE      = 0x07FF;  // ciano — info

// Cores por mao (hand picker)
static constexpr uint16_t COL_HF_LEFT  = 0xFB80;  // laranja — esquerda
static constexpr uint16_t COL_HF_RIGHT = 0x4D9F;  // azul-claro — direita
static constexpr uint16_t COL_HF_SOLO  = 0xFFE0;  // amarelo — solo

// Status do gesto
static constexpr uint16_t COL_TRAINED    = 0x07E0;  // verde
static constexpr uint16_t COL_UNTRAINED  = 0x9CD3;  // cinza medio

// Labels MAIN
static const char* const MAIN_LABEL_LINE1[MAIN_COUNT] = {
    "TREINAR",
    "APAGAR",
    "BLUETOOTH",
    "CANAL",
    "SILENCIO",
    "SOBRE"
};
static const char* const MAIN_LABEL_LINE2[MAIN_COUNT] = {
    "gesto",
    "gesto",
    "PWA conectar",
    "Wi-Fi (v2)",
    "modo on/off",
    "info dispositivo"
};
static const uint16_t MAIN_COLORS[MAIN_COUNT] = {
    COL_TREINAR, COL_APAGAR, COL_BLUETOOTH, COL_CANAL, COL_SILENCIOSO, COL_SOBRE
};

// Labels HAND PICKER
static const char* const HAND_LABEL_LINE1[HF_COUNT] = {
    "ESQUERDA", "DIREITA", "SOLO"
};
static const char* const HAND_LABEL_LINE2[HF_COUNT] = {
    "contexto", "objeto", "independente"
};
static const uint16_t HAND_COLORS[HF_COUNT] = {
    COL_HF_LEFT, COL_HF_RIGHT, COL_HF_SOLO
};

// ============================================================================
// Lifecycle
// ============================================================================

void MenuUI::begin() {
    _level = MENU_LEVEL_NONE;
    _mainCursor = 0;
    _subKind = SUB_NONE;
    _handPickerCursor = 0;
    _handFilter = HF_LEFT;
    _gestureCursor = 0;
    _filteredCount = 0;
    _pendingTrainValid = false;
    _pendingTrainIdStr[0] = '\0';
    _pendingTrainName[0] = '\0';
    _pendingDeleteValid = false;
    _pendingDeleteIdStr[0] = '\0';
    _pendingDeleteName[0] = '\0';
    _pendingBluetoothActivate = false;
    _pendingSilentToggle = false;
    _actionKind = ACTION_PLACEHOLDER;
    _deleteConfirmIdStr[0] = '\0';
    _deleteConfirmName[0] = '\0';
    _dirty = true;
}

void MenuUI::enter() {
    _level = MENU_LEVEL_MAIN;
    _mainCursor = 0;
    _subKind = SUB_NONE;
    _dirty = true;
    _enterMs = millis();
    Serial.println("[MenuUI v2] Aberto no MAIN cursor=0");
}

void MenuUI::exit() {
    _level = MENU_LEVEL_NONE;
    _subKind = SUB_NONE;
    _dirty = true;
    Serial.println("[MenuUI v2] Fechado");
}

// ============================================================================
// Filtro de gestos por mao
// ============================================================================

void MenuUI::rebuildFilteredList(HandFilter hand) {
    _filteredCount = 0;
    int total = gestureEngine.getGestureCount();
    for (int i = 0; i < total && _filteredCount < MAX_FILTERED; i++) {
        const GestureDefinition& g = gestureEngine.getGesture(i);
        bool match = false;
        switch (hand) {
            case HF_LEFT:  match = g.isContext; break;
            case HF_RIGHT: match = !g.isContext && !g.isSolo; break;
            case HF_SOLO:  match = g.isSolo; break;
            default: break;
        }
        if (match) {
            _filteredIndices[_filteredCount++] = i;
        }
    }
    Serial.printf("[MenuUI v2] Filter hand=%d -> %d gestos\n", hand, _filteredCount);
}

// ============================================================================
// Entradas em sub-menus
// ============================================================================

void MenuUI::enterTrainHandPicker() {
    _level = MENU_LEVEL_SUB;
    _subKind = SUB_TRAIN_HAND_PICK;
    _handPickerCursor = 0;
    _dirty = true;
    Serial.println("[MenuUI v2] -> SUB_TRAIN_HAND_PICK");
}

void MenuUI::enterDeleteHandPicker() {
    _level = MENU_LEVEL_SUB;
    _subKind = SUB_DELETE_HAND_PICK;
    _handPickerCursor = 0;
    _dirty = true;
    Serial.println("[MenuUI v2] -> SUB_DELETE_HAND_PICK");
}

void MenuUI::enterGestureCarousel(SubmenuKind kind, HandFilter hand) {
    _level = MENU_LEVEL_SUB;
    _subKind = kind;
    _handFilter = hand;
    _gestureCursor = 0;
    rebuildFilteredList(hand);
    _dirty = true;
    Serial.printf("[MenuUI v2] -> SUB gesture (kind=%d hand=%d count=%d)\n",
                  kind, hand, _filteredCount);
}

void MenuUI::reopenAtTrainGesture() {
    _level = MENU_LEVEL_SUB;
    _subKind = SUB_TRAIN_GESTURE;
    rebuildFilteredList(_handFilter);
    if (_gestureCursor >= _filteredCount) _gestureCursor = 0;
    _dirty = true;
}

void MenuUI::reopenAtDeleteGesture() {
    _level = MENU_LEVEL_SUB;
    _subKind = SUB_DELETE_GESTURE;
    rebuildFilteredList(_handFilter);
    if (_gestureCursor >= _filteredCount) _gestureCursor = 0;
    _dirty = true;
}

void MenuUI::reopenAtSilentScreen() {
    _level = MENU_LEVEL_ACTION;
    _actionKind = ACTION_SILENT;
    _dirty = true;
}

// ============================================================================
// Navegacao — 1xB (next), 2xB (prev), 1xA (confirm), Hold B (back)
// ============================================================================

void MenuUI::onNavigateNext() {
    if (_level == MENU_LEVEL_NONE) return;

    if (_level == MENU_LEVEL_MAIN) {
        _mainCursor = (_mainCursor + 1) % MAIN_COUNT;
    } else if (_level == MENU_LEVEL_SUB) {
        if (_subKind == SUB_TRAIN_HAND_PICK || _subKind == SUB_DELETE_HAND_PICK) {
            _handPickerCursor = (_handPickerCursor + 1) % HF_COUNT;
        } else if ((_subKind == SUB_TRAIN_GESTURE || _subKind == SUB_DELETE_GESTURE)
                   && _filteredCount > 0) {
            _gestureCursor = (_gestureCursor + 1) % _filteredCount;
        }
    }
    // Em ACTION, 1xB sem efeito (telas finais nao tem carrossel)
    _dirty = true;
}

void MenuUI::onNavigatePrev() {
    if (_level == MENU_LEVEL_NONE) return;

    if (_level == MENU_LEVEL_MAIN) {
        _mainCursor = (_mainCursor + MAIN_COUNT - 1) % MAIN_COUNT;
    } else if (_level == MENU_LEVEL_SUB) {
        if (_subKind == SUB_TRAIN_HAND_PICK || _subKind == SUB_DELETE_HAND_PICK) {
            _handPickerCursor = (_handPickerCursor + HF_COUNT - 1) % HF_COUNT;
        } else if ((_subKind == SUB_TRAIN_GESTURE || _subKind == SUB_DELETE_GESTURE)
                   && _filteredCount > 0) {
            _gestureCursor = (_gestureCursor + _filteredCount - 1) % _filteredCount;
        }
    }
    _dirty = true;
}

void MenuUI::onBackToParent() {
    if (_level == MENU_LEVEL_NONE) return;

    if (_level == MENU_LEVEL_MAIN) {
        // No topo — Hold B no MAIN nao faz nada (3xB e que sai)
        return;
    }
    if (_level == MENU_LEVEL_SUB) {
        if (_subKind == SUB_TRAIN_GESTURE) {
            // Volta pro hand picker do treinar
            enterTrainHandPicker();
        } else if (_subKind == SUB_DELETE_GESTURE) {
            enterDeleteHandPicker();
        } else {
            // Hand picker -> volta MAIN
            _level = MENU_LEVEL_MAIN;
            _subKind = SUB_NONE;
            _dirty = true;
        }
        return;
    }
    if (_level == MENU_LEVEL_ACTION) {
        // ACTION -> volta MAIN (ACTIONs sao folhas)
        _level = MENU_LEVEL_MAIN;
        _actionKind = ACTION_PLACEHOLDER;
        _dirty = true;
    }
}

void MenuUI::onConfirm() {
    if (_level == MENU_LEVEL_NONE) return;

    if (_level == MENU_LEVEL_MAIN) {
        switch (_mainCursor) {
            case MAIN_TREINAR:
                enterTrainHandPicker();
                return;
            case MAIN_APAGAR:
                enterDeleteHandPicker();
                return;
            case MAIN_BLUETOOTH:
                _pendingBluetoothActivate = true;
                _level = MENU_LEVEL_NONE;
                _dirty = true;
                Serial.println("[MenuUI v2] BT ativacao solicitada");
                return;
            case MAIN_SOBRE:
                _actionKind = ACTION_ABOUT;
                _level = MENU_LEVEL_ACTION;
                _dirty = true;
                return;
            case MAIN_SILENCIOSO:
                _actionKind = ACTION_SILENT;
                _level = MENU_LEVEL_ACTION;
                _dirty = true;
                return;
            case MAIN_CANAL:
                _actionKind = ACTION_PLACEHOLDER;
                _level = MENU_LEVEL_ACTION;
                _dirty = true;
                return;
            case MAIN_COUNT:
            default:
                Serial.printf("[MenuUI v2] WARN cursor invalido %d\n", _mainCursor);
                return;
        }
    }

    if (_level == MENU_LEVEL_SUB) {
        if (_subKind == SUB_TRAIN_HAND_PICK) {
            enterGestureCarousel(SUB_TRAIN_GESTURE, (HandFilter)_handPickerCursor);
            return;
        }
        if (_subKind == SUB_DELETE_HAND_PICK) {
            enterGestureCarousel(SUB_DELETE_GESTURE, (HandFilter)_handPickerCursor);
            return;
        }
        if (_subKind == SUB_TRAIN_GESTURE && _filteredCount > 0) {
            int idx = _filteredIndices[_gestureCursor];
            const GestureDefinition& g = gestureEngine.getGesture(idx);
            strncpy(_pendingTrainIdStr, g.idStr, sizeof(_pendingTrainIdStr) - 1);
            _pendingTrainIdStr[sizeof(_pendingTrainIdStr) - 1] = '\0';
            strncpy(_pendingTrainName, g.name, sizeof(_pendingTrainName) - 1);
            _pendingTrainName[sizeof(_pendingTrainName) - 1] = '\0';
            _pendingTrainValid = true;
            _level = MENU_LEVEL_NONE;
            _dirty = true;
            Serial.printf("[MenuUI v2] Treino solicitado: %s (%s)\n",
                          _pendingTrainIdStr, _pendingTrainName);
            return;
        }
        if (_subKind == SUB_DELETE_GESTURE && _filteredCount > 0) {
            int idx = _filteredIndices[_gestureCursor];
            const GestureDefinition& g = gestureEngine.getGesture(idx);
            strncpy(_deleteConfirmIdStr, g.idStr, sizeof(_deleteConfirmIdStr) - 1);
            _deleteConfirmIdStr[sizeof(_deleteConfirmIdStr) - 1] = '\0';
            strncpy(_deleteConfirmName, g.name, sizeof(_deleteConfirmName) - 1);
            _deleteConfirmName[sizeof(_deleteConfirmName) - 1] = '\0';
            _actionKind = ACTION_DELETE_CONFIRM;
            _level = MENU_LEVEL_ACTION;
            _dirty = true;
            return;
        }
    }

    if (_level == MENU_LEVEL_ACTION) {
        if (_actionKind == ACTION_DELETE_CONFIRM) {
            strncpy(_pendingDeleteIdStr, _deleteConfirmIdStr, sizeof(_pendingDeleteIdStr) - 1);
            _pendingDeleteIdStr[sizeof(_pendingDeleteIdStr) - 1] = '\0';
            strncpy(_pendingDeleteName, _deleteConfirmName, sizeof(_pendingDeleteName) - 1);
            _pendingDeleteName[sizeof(_pendingDeleteName) - 1] = '\0';
            _pendingDeleteValid = true;
            _level = MENU_LEVEL_NONE;
            _dirty = true;
            return;
        }
        if (_actionKind == ACTION_SILENT) {
            _pendingSilentToggle = true;
            _level = MENU_LEVEL_NONE;
            _dirty = true;
            return;
        }
    }
}

// ============================================================================
// Pendentes one-shot
// ============================================================================

bool MenuUI::consumePendingTrain(char* idStrOut, size_t idStrSz,
                                  char* nameOut, size_t nameSz) {
    if (!_pendingTrainValid) return false;
    if (idStrOut && idStrSz > 0) {
        strncpy(idStrOut, _pendingTrainIdStr, idStrSz - 1);
        idStrOut[idStrSz - 1] = '\0';
    }
    if (nameOut && nameSz > 0) {
        strncpy(nameOut, _pendingTrainName, nameSz - 1);
        nameOut[nameSz - 1] = '\0';
    }
    _pendingTrainValid = false;
    _pendingTrainIdStr[0] = '\0';
    _pendingTrainName[0] = '\0';
    return true;
}

bool MenuUI::consumePendingDelete(char* idStrOut, size_t idStrSz,
                                   char* nameOut, size_t nameSz) {
    if (!_pendingDeleteValid) return false;
    if (idStrOut && idStrSz > 0) {
        strncpy(idStrOut, _pendingDeleteIdStr, idStrSz - 1);
        idStrOut[idStrSz - 1] = '\0';
    }
    if (nameOut && nameSz > 0) {
        strncpy(nameOut, _pendingDeleteName, nameSz - 1);
        nameOut[nameSz - 1] = '\0';
    }
    _pendingDeleteValid = false;
    _pendingDeleteIdStr[0] = '\0';
    _pendingDeleteName[0] = '\0';
    return true;
}

bool MenuUI::consumePendingBluetoothActivate() {
    if (!_pendingBluetoothActivate) return false;
    _pendingBluetoothActivate = false;
    return true;
}

bool MenuUI::consumePendingSilentToggle() {
    if (!_pendingSilentToggle) return false;
    _pendingSilentToggle = false;
    return true;
}

// ============================================================================
// RENDER
// ============================================================================

void MenuUI::render() {
    if (!_dirty) return;
    _dirty = false;
    if (_level == MENU_LEVEL_NONE) return;

    if (_level == MENU_LEVEL_MAIN) {
        renderMainCarousel();
    } else if (_level == MENU_LEVEL_SUB) {
        if (_subKind == SUB_TRAIN_HAND_PICK) {
            renderHandPicker("TREINAR");
        } else if (_subKind == SUB_DELETE_HAND_PICK) {
            renderHandPicker("APAGAR");
        } else if (_subKind == SUB_TRAIN_GESTURE) {
            renderGestureCarousel("TREINAR", false);
        } else if (_subKind == SUB_DELETE_GESTURE) {
            renderGestureCarousel("APAGAR", true);
        }
    } else if (_level == MENU_LEVEL_ACTION) {
        if (_actionKind == ACTION_DELETE_CONFIRM) {
            renderActionDeleteConfirm();
        } else if (_actionKind == ACTION_ABOUT) {
            renderActionAbout();
        } else if (_actionKind == ACTION_SILENT) {
            renderActionSilent();
        } else {
            renderActionPlaceholder(MAIN_LABEL_LINE1[_mainCursor]);
        }
    }
}

// ============================================================================
// Helpers de render comuns
// ============================================================================

void MenuUI::drawHeader(uint16_t color, const char* title, const char* counter) {
    auto& d = StickCP2.Display;
    d.fillRect(0, 0, 240, 18, color);
    d.setTextColor(COLOR_TEXT, color);
    d.setTextSize(1);
    d.setCursor(6, 5);
    d.print(title);
    if (counter && counter[0]) {
        int counterX = 240 - 6 * static_cast<int>(strlen(counter)) - 4;
        d.setCursor(counterX, 5);
        d.print(counter);
    }
}

void MenuUI::drawFooter(const char* legend) {
    auto& d = StickCP2.Display;
    d.fillRect(0, 124, 240, 11, COLOR_FOOTER);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_FOOTER);
    d.setTextSize(1);
    d.setCursor(4, 126);
    d.print(legend);
}

void MenuUI::drawCenteredTitle(const char* line1, const char* line2) {
    auto& d = StickCP2.Display;
    // Title size 3 (~18px wide per char). Centralizar.
    d.setTextSize(3);
    int w1 = strlen(line1) * 18;
    int x1 = (240 - w1) / 2;
    if (x1 < 4) x1 = 4;
    d.setTextColor(COLOR_TEXT, COLOR_BG);
    d.setCursor(x1, 45);
    d.print(line1);

    // Subtitle size 2 (~12px wide per char)
    if (line2 && line2[0]) {
        d.setTextSize(2);
        int w2 = strlen(line2) * 12;
        int x2 = (240 - w2) / 2;
        if (x2 < 4) x2 = 4;
        d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
        d.setCursor(x2, 80);
        d.print(line2);
    }
}

// ============================================================================
// Renders especificos
// ============================================================================

void MenuUI::renderMainCarousel() {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);

    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", _mainCursor + 1, MAIN_COUNT);
    drawHeader(MAIN_COLORS[_mainCursor], "MENU", counter);
    drawCenteredTitle(MAIN_LABEL_LINE1[_mainCursor], MAIN_LABEL_LINE2[_mainCursor]);
    drawFooter("1A OK  HoldB volta  3xB sai");
}

void MenuUI::renderHandPicker(const char* title) {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);

    char counter[16];
    snprintf(counter, sizeof(counter), "%d/%d", _handPickerCursor + 1, HF_COUNT);
    drawHeader(HAND_COLORS[_handPickerCursor], title, counter);
    drawCenteredTitle(HAND_LABEL_LINE1[_handPickerCursor], HAND_LABEL_LINE2[_handPickerCursor]);
    drawFooter("1A entrar  HoldB volta");
}

void MenuUI::renderGestureCarousel(const char* title, bool deleteMode) {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);

    // Header com cor da mao + contador
    char counter[20];
    if (_filteredCount > 0) {
        snprintf(counter, sizeof(counter), "%d/%d", _gestureCursor + 1, _filteredCount);
    } else {
        snprintf(counter, sizeof(counter), "(vazio)");
    }
    char headerTitle[32];
    snprintf(headerTitle, sizeof(headerTitle), "%s %s", title, HAND_LABEL_LINE1[_handFilter]);
    drawHeader(HAND_COLORS[_handFilter], headerTitle, counter);

    if (_filteredCount <= 0) {
        d.setTextSize(2);
        d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
        d.setCursor(20, 60);
        d.print("Nenhum gesto");
        drawFooter("HoldB volta");
        return;
    }

    int idx = _filteredIndices[_gestureCursor];
    const GestureDefinition& g = gestureEngine.getGesture(idx);

    // UX-v2-fix (2026-05-03): swap status<->id por feedback do dono.
    // Antes: status TREINADO/VAZIO size 1 (escondido) em cima; ID size 2 embaixo.
    // Agora: ID size 1 (debug, pequeno) em cima; status size 2 (informacao
    // util) embaixo, colorido. Decisao: usuario nao precisa ver ID, mas
    // precisa ver claramente se o gesto esta treinado.

    // ID pequeno em cima (size 1, cinza dim) - debug/referencia tecnica
    d.setTextSize(1);
    int idw = strlen(g.idStr) * 6;
    int idx_x = (240 - idw) / 2;
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    d.setCursor(idx_x, 24);
    d.print(g.idStr);

    // Nome grande no centro (size 3, branco) - truncado se >13 chars
    char nameUpper[16];
    int n = strlen(g.name);
    if (n > 13) n = 13;
    for (int i = 0; i < n; i++) {
        char c = g.name[i];
        nameUpper[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    nameUpper[n] = '\0';
    d.setTextSize(3);
    int nw = strlen(nameUpper) * 18;
    int nx = (240 - nw) / 2;
    if (nx < 4) nx = 4;
    d.setTextColor(COLOR_TEXT, COLOR_BG);
    d.setCursor(nx, 50);
    d.print(nameUpper);

    // Status medio embaixo (size 2, cor por estado) - informacao util
    uint16_t statusCol = g.trained ? COL_TRAINED : COL_UNTRAINED;
    const char* statusTxt = g.trained ? "TREINADO" : "VAZIO";
    d.setTextSize(2);
    int sw = strlen(statusTxt) * 12;
    int sx = (240 - sw) / 2;
    d.setTextColor(statusCol, COLOR_BG);
    d.setCursor(sx, 95);
    d.print(statusTxt);

    // Footer adapta texto ao modo
    drawFooter(deleteMode ? "1A apagar  HoldB volta" : "1A treinar  HoldB volta");
}

void MenuUI::renderActionPlaceholder(const char* itemName) {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);
    drawHeader(COL_CANAL, itemName, "");

    d.setTextSize(2);
    d.setTextColor(COLOR_TEXT, COLOR_BG);
    d.setCursor(20, 50);
    d.print("Em construcao");
    d.setTextSize(1);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    d.setCursor(20, 80);
    d.print("Reservado pra v2 (D-C3-01)");

    drawFooter("HoldB volta  3xB sai");
}

void MenuUI::renderActionDeleteConfirm() {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);
    drawHeader(COL_APAGAR, "APAGAR GESTO?", "");

    d.setTextSize(1);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    d.setCursor(8, 26);
    d.print("Apagar treino de:");

    // Nome grande
    char nameUpper[16];
    int n = strlen(_deleteConfirmName);
    if (n > 13) n = 13;
    for (int i = 0; i < n; i++) {
        char c = _deleteConfirmName[i];
        nameUpper[i] = (c >= 'a' && c <= 'z') ? (c - 32) : c;
    }
    nameUpper[n] = '\0';

    d.setTextSize(3);
    int w = strlen(nameUpper) * 18;
    int x = (240 - w) / 2;
    if (x < 4) x = 4;
    d.setTextColor(COLOR_TEXT, COLOR_BG);
    d.setCursor(x, 48);
    d.print(nameUpper);

    d.setTextSize(1);
    d.setTextColor(COL_APAGAR, COLOR_BG);
    d.setCursor(8, 90);
    d.print("Trajetoria sera perdida.");

    drawFooter("1A APAGAR  HoldB cancelar");
}

void MenuUI::renderActionAbout() {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);
    drawHeader(COL_SOBRE, "SOBRE", "");

    d.setTextSize(1);
    d.setTextColor(COLOR_TEXT, COLOR_BG);

    // Coleta dados runtime
    int total = gestureEngine.getGestureCount();
    int trained = 0;
    for (int i = 0; i < total; i++) {
        if (gestureEngine.getGesture(i).trained) trained++;
    }
    uint8_t mac[6] = {0};
    WiFi.macAddress(mac);
    uint32_t freeHeap = ESP.getFreeHeap() / 1024;

    char line[48];
    int y = 24;
    snprintf(line, sizeof(line), "Firmware: v%s", GESTUUM_FW_VERSION);
    d.setCursor(6, y); d.print(line); y += 14;

    snprintf(line, sizeof(line), "MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    d.setCursor(6, y); d.print(line); y += 14;

    snprintf(line, sizeof(line), "Gestos: %d treinados / %d", trained, total);
    d.setCursor(6, y); d.print(line); y += 14;

    snprintf(line, sizeof(line), "RAM livre: %u KB", static_cast<unsigned>(freeHeap));
    d.setCursor(6, y); d.print(line); y += 14;

    snprintf(line, sizeof(line), "Canal ESP-NOW: %d", espnow_get_channel());
    d.setCursor(6, y); d.print(line);

    drawFooter("HoldB volta  3xB sai");
}

void MenuUI::renderActionSilent() {
    auto& d = StickCP2.Display;
    d.fillScreen(COLOR_BG);
    drawHeader(COL_SILENCIOSO, "MODO SILENCIOSO", "");

    bool isOn = audioPlayer.isSilentAll();

    d.setTextSize(1);
    d.setTextColor(COLOR_TEXT, COLOR_BG);
    d.setCursor(8, 30);
    d.print("Estado atual:");

    d.setTextSize(4);
    if (isOn) {
        d.setTextColor(COL_APAGAR, COLOR_BG);
        d.setCursor(80, 50);
        d.print("ON");
    } else {
        d.setTextColor(COL_TRAINED, COLOR_BG);
        d.setCursor(70, 50);
        d.print("OFF");
    }

    d.setTextSize(1);
    d.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    d.setCursor(8, 100);
    if (isOn) {
        d.print("Sons suprimidos (SOS toca)");
    } else {
        d.print("Sons normais");
    }

    drawFooter("1A alternar  HoldB volta");
}
