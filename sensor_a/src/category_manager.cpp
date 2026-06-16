/**
 * GESTUUM — Category Manager Implementation (Sensor A)
 * Bloco: 3.5 — Category Manager (5 categories)
 * Responsibility: Manage active gesture category with cycle, direction, and emergency selection.
 */

#include "category_manager.h"

// --- Lookup tables indexed by Category enum ---

static const char* const CATEGORY_NAMES[CAT_COUNT] = {
    "GERAL",
    "EMERGENCIA",
    "CASA",
    "TRABALHO",
    "SOCIAL"
};

static const uint32_t CATEGORY_COLORS[CAT_COUNT] = {
    COLOR_GERAL,       // 0x0000FF — Blue
    COLOR_EMERGENCIA,  // 0xFF0000 — Red
    COLOR_CASA,        // 0x00FF00 — Green
    COLOR_TRABALHO,    // 0xFFFF00 — Yellow
    COLOR_SOCIAL       // 0x8000FF — Purple
};

static const float CATEGORY_THRESHOLDS[CAT_COUNT] = {
    DTW_THRESHOLD_DEFAULT,    // GERAL:      3.5
    DTW_THRESHOLD_EMERGENCY,  // EMERGENCIA: 4.5 (more tolerant)
    DTW_THRESHOLD_DEFAULT,    // CASA:       3.5
    DTW_THRESHOLD_DEFAULT,    // TRABALHO:   3.5
    DTW_THRESHOLD_DEFAULT     // SOCIAL:     3.5
};

// --- Constructor ---

CategoryManager::CategoryManager()
    : current(CAT_GERAL),
      buttonBHeld(false),
      lastAx(0.0f),
      lastAy(0.0f),
      lastAz(0.0f),
      onChangeCallback(nullptr) {
}

// --- Public methods ---

void CategoryManager::begin() {
    current = CAT_GERAL;
    buttonBHeld = false;
    lastAx = 0.0f;
    lastAy = 0.0f;
    lastAz = 0.0f;
}

void CategoryManager::update(float ax, float ay, float az) {
    // Always store latest acceleration for direction detection on button release
    lastAx = ax;
    lastAy = ay;
    lastAz = az;
}

Category CategoryManager::getCurrentCategory() const {
    return current;
}

void CategoryManager::setCategory(Category cat) {
    if (cat >= CAT_COUNT) {
        return;  // Ignore invalid category
    }
    if (cat != current) {
        current = cat;
        notifyChange();
    }
}

void CategoryManager::nextCategory() {
    current = static_cast<Category>((static_cast<uint8_t>(current) + 1) % CAT_COUNT);
    notifyChange();
}

void CategoryManager::previousCategory() {
    uint8_t idx = static_cast<uint8_t>(current);
    current = static_cast<Category>((idx == 0) ? (CAT_COUNT - 1) : (idx - 1));
    notifyChange();
}

void CategoryManager::setEmergency() {
    setCategory(CAT_EMERGENCIA);
}

// --- Button event handlers ---

void CategoryManager::onButtonBClick() {
    nextCategory();
}

void CategoryManager::onButtonBHold(bool holding) {
    if (holding) {
        // Start tracking — direction will be detected on release
        buttonBHeld = true;
    } else if (buttonBHeld) {
        // Released after hold — detect direction from last captured acceleration
        buttonBHeld = false;
        Category detected = detectDirection(lastAx, lastAy, lastAz);
        setCategory(detected);
    }
}

void CategoryManager::onButtonALongPress() {
    setEmergency();
}

void CategoryManager::setOnChangeCallback(CategoryChangeCallback cb) {
    onChangeCallback = cb;
}

Category CategoryManager::detectDirection(float ax, float ay, float az) {
    (void)az;  // az not used for 2D direction mapping

    // Priority: check Y axis first (up/down), then X axis (left/right)
    if (ay > DIRECTION_THRESHOLD) {
        return CAT_EMERGENCIA;  // Tilt up
    }
    if (ay < -DIRECTION_THRESHOLD) {
        return CAT_CASA;        // Tilt down
    }
    if (ax < -DIRECTION_THRESHOLD) {
        return CAT_TRABALHO;    // Tilt left
    }
    if (ax > DIRECTION_THRESHOLD) {
        return CAT_SOCIAL;      // Tilt right
    }
    return CAT_GERAL;           // No significant tilt — center
}

float CategoryManager::getThreshold() const {
    return CATEGORY_THRESHOLDS[current];
}

const char* CategoryManager::getCategoryName() const {
    return CATEGORY_NAMES[current];
}

uint32_t CategoryManager::getCategoryColor() const {
    return CATEGORY_COLORS[current];
}

bool CategoryManager::isEmergency() const {
    return current == CAT_EMERGENCIA;
}

// --- Private ---

void CategoryManager::notifyChange() {
    if (onChangeCallback != nullptr) {
        onChangeCallback(current);
    }
}
