/**
 * GESTUUM — Category Manager (Sensor A)
 * Bloco: 3.5 — Category Manager (5 categories)
 * Responsibility: Manage active gesture category with cycle, direction, and emergency selection.
 */

#ifndef GESTUUM_CATEGORY_MANAGER_H
#define GESTUUM_CATEGORY_MANAGER_H

#include "constants.h"
#include <stdint.h>

// Direction detection threshold in g units
#define DIRECTION_THRESHOLD 0.5f

// Callback type for category change notification
typedef void (*CategoryChangeCallback)(Category newCategory);

class CategoryManager {
public:
    CategoryManager();

    /**
     * Initialize manager — sets category to CAT_GERAL.
     */
    void begin();

    /**
     * Called every loop iteration with current IMU acceleration data.
     * When Button B is held, captures acceleration for direction detection.
     */
    void update(float ax, float ay, float az);

    /**
     * Get the currently active category.
     */
    Category getCurrentCategory() const;

    /**
     * Set category directly and notify listeners.
     */
    void setCategory(Category cat);

    /**
     * Cycle to next category: GERAL -> EMERGENCIA -> CASA -> TRABALHO -> SOCIAL -> GERAL.
     */
    void nextCategory();

    /**
     * Cycle to previous category with wrap-around (0 wraps to CAT_COUNT-1).
     * UX6: Bidirectional navigation — reduces clicks to reach any category.
     */
    void previousCategory();

    /**
     * Shortcut: set category to CAT_EMERGENCIA immediately.
     */
    void setEmergency();

    // --- Button event handlers (called from main loop) ---

    /**
     * Handle Button B single click — cycles to next category.
     */
    void onButtonBClick();

    /**
     * Handle Button B hold state change.
     * @param holding true when button is being held, false on release.
     */
    void onButtonBHold(bool holding);

    /**
     * Handle Button A long press (2s) — emergency shortcut.
     */
    void onButtonALongPress();

    /**
     * Register a callback to be invoked on category change.
     */
    void setOnChangeCallback(CategoryChangeCallback cb);

    /**
     * Detect intended category from accelerometer tilt direction.
     * Up (ay > threshold)       -> CAT_EMERGENCIA
     * Down (ay < -threshold)    -> CAT_CASA
     * Left (ax < -threshold)    -> CAT_TRABALHO
     * Right (ax > threshold)    -> CAT_SOCIAL
     * Center (no tilt)          -> CAT_GERAL
     */
    Category detectDirection(float ax, float ay, float az);

    /**
     * Get the DTW threshold for the current category.
     */
    float getThreshold() const;

    /**
     * Get a human-readable name for the current category.
     */
    const char* getCategoryName() const;

    /**
     * Get the RGB hex color for the current category.
     */
    uint32_t getCategoryColor() const;

    /**
     * Check if the current category is EMERGENCIA.
     */
    bool isEmergency() const;

private:
    Category current;
    bool buttonBHeld;
    float lastAx;
    float lastAy;
    float lastAz;
    CategoryChangeCallback onChangeCallback;

    /**
     * Invoke the registered callback, if any.
     */
    void notifyChange();
};

#endif // GESTUUM_CATEGORY_MANAGER_H
