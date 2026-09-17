#ifndef PANEL_CONFIG_H
#define PANEL_CONFIG_H

#define PANEL_WIDTH 16
#define PANEL_HEIGHT 16
#define PANEL_LED_COUNT (PANEL_WIDTH * PANEL_HEIGHT)

// 1: odd rows reversed, 0: normal row-major.
#define PANEL_SERPENTINE 1

// Unified max value for LED channels used by all simulators.
#define PANEL_LED_VALUE_MAX 20

static inline int panel_led_index(int x, int y) {
#if PANEL_SERPENTINE
    if (y & 1) {
        return y * PANEL_WIDTH + (PANEL_WIDTH - 1 - x);
    }
#endif
    return y * PANEL_WIDTH + x;
}

#endif
