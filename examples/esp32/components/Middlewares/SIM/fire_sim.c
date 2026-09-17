#include "fire_sim.h"

#include <math.h>
#include <stdint.h>

#include "doom.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gravity.h"
#include "key.h"
#include "panel_config.h"
#include "rgb.h"

#define W PANEL_WIDTH
#define H PANEL_HEIGHT

#define SIM_FPS 30
#define LED_VAL_MAX_I PANEL_LED_VALUE_MAX

#define KEY_GPIO_PIN GPIO_NUM_0
#define KEY_DEBOUNCE_MS 50

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} rgb8_t;

static const char* TAG = "fire_sim";

#define PAL_SIZE 37
#define PALETTE_COUNT 3

static TaskHandle_t s_task = NULL;
static volatile bool s_running = false;
static key_t s_key;
static uint8_t s_palette_idx = 0;
static uint16_t s_fire_led_map[W * H];
static bool s_fire_led_map_ready = false;

static const uint8_t PAL_CLASSIC[PAL_SIZE][3] = {
    {0x07, 0x07, 0x07}, {0x1f, 0x07, 0x07}, {0x2f, 0x0f, 0x07}, {0x47, 0x0f, 0x07}, {0x57, 0x17, 0x07},
    {0x67, 0x1f, 0x07}, {0x77, 0x1f, 0x07}, {0x8f, 0x27, 0x07}, {0x9f, 0x2f, 0x07}, {0xaf, 0x3f, 0x07},
    {0xbf, 0x47, 0x07}, {0xc7, 0x47, 0x07}, {0xdf, 0x4f, 0x07}, {0xdf, 0x57, 0x07}, {0xdf, 0x57, 0x07},
    {0xd7, 0x5f, 0x07}, {0xd7, 0x67, 0x0f}, {0xcf, 0x6f, 0x0f}, {0xcf, 0x77, 0x0f}, {0xcf, 0x7f, 0x0f},
    {0xcf, 0x87, 0x17}, {0xc7, 0x87, 0x17}, {0xc7, 0x8f, 0x17}, {0xc7, 0x97, 0x1f}, {0xbf, 0x9f, 0x1f},
    {0xbf, 0x9f, 0x1f}, {0xbf, 0xa7, 0x27}, {0xbf, 0xa7, 0x27}, {0xbf, 0xaf, 0x2f}, {0xb7, 0xaf, 0x2f},
    {0xb7, 0xb7, 0x2f}, {0xb7, 0xb7, 0x37}, {0xcf, 0xcf, 0x6f}, {0xdf, 0xdf, 0x9f}, {0xef, 0xef, 0xc7},
    {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff},
};

static const uint8_t PAL_BLUE[PAL_SIZE][3] = {
    {0x05, 0x05, 0x05}, {0x05, 0x05, 0x10}, {0x05, 0x05, 0x20}, {0x05, 0x05, 0x30}, {0x05, 0x05, 0x40},
    {0x09, 0x09, 0x50}, {0x09, 0x09, 0x60}, {0x09, 0x09, 0x70}, {0x09, 0x09, 0x80}, {0x09, 0x09, 0x90},
    {0x0f, 0x0f, 0xa0}, {0x0f, 0x0f, 0xb0}, {0x1f, 0x1f, 0xc0}, {0x2f, 0x2f, 0xd0}, {0x3f, 0x3f, 0xe0},
    {0x4f, 0x4f, 0xf0}, {0x5f, 0x5f, 0xff}, {0x6f, 0x6f, 0xff}, {0x7f, 0x7f, 0xff}, {0x8f, 0x8f, 0xff},
    {0x9f, 0x9f, 0xff}, {0xaf, 0xaf, 0xff}, {0xbf, 0xbf, 0xff}, {0xcf, 0xcf, 0xff}, {0xdf, 0xdf, 0xff},
    {0xef, 0xef, 0xff}, {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff}, {0xe0, 0xff, 0xff}, {0xd0, 0xff, 0xff},
    {0xc0, 0xff, 0xff}, {0xb0, 0xff, 0xff}, {0xa0, 0xff, 0xff}, {0x90, 0xff, 0xff}, {0x80, 0xff, 0xff},
    {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff},
};

static const uint8_t PAL_PURPLE[PAL_SIZE][3] = {
    {0x05, 0x00, 0x05}, {0x10, 0x00, 0x10}, {0x20, 0x00, 0x20}, {0x30, 0x00, 0x30}, {0x40, 0x00, 0x40},
    {0x50, 0x00, 0x50}, {0x60, 0x05, 0x60}, {0x70, 0x05, 0x70}, {0x80, 0x05, 0x80}, {0x90, 0x0a, 0x90},
    {0xa0, 0x0a, 0xa0}, {0xb0, 0x0a, 0xb0}, {0xc0, 0x10, 0xc0}, {0xd0, 0x15, 0xd0}, {0xe0, 0x20, 0xe0},
    {0xf0, 0x30, 0xf0}, {0xff, 0x40, 0xff}, {0xff, 0x50, 0xff}, {0xff, 0x60, 0xff}, {0xff, 0x70, 0xff},
    {0xff, 0x80, 0xff}, {0xff, 0x90, 0xff}, {0xff, 0xa0, 0xff}, {0xff, 0xb0, 0xff}, {0xff, 0xc0, 0xff},
    {0xff, 0xd0, 0xff}, {0xff, 0xe0, 0xff}, {0xff, 0xf0, 0xff}, {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff},
    {0xfd, 0xee, 0xfd}, {0xfb, 0xdd, 0xfb}, {0xf9, 0xcc, 0xf9}, {0xf7, 0xbb, 0xf7}, {0xf5, 0xaa, 0xf5},
    {0xff, 0xff, 0xff}, {0xff, 0xff, 0xff},
};

static rgb8_t s_lut[PALETTE_COUNT][DOOM_HEAT_LEVELS];
static uint8_t s_lut_ready[PALETTE_COUNT];

static inline const uint8_t (*get_palette(uint8_t idx))[3] {
    if (idx == 1) {
        return PAL_BLUE;
    }
    if (idx == 2) {
        return PAL_PURPLE;
    }
    return PAL_CLASSIC;
}

static inline void scale_rgb_to_ledmax(uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = (uint8_t)(((uint16_t)(*r) * LED_VAL_MAX_I) / 255u);
    *g = (uint8_t)(((uint16_t)(*g) * LED_VAL_MAX_I) / 255u);
    *b = (uint8_t)(((uint16_t)(*b) * LED_VAL_MAX_I) / 255u);
}

static void build_lut(uint8_t pal_idx) {
    const uint8_t (*pal)[3] = get_palette(pal_idx);

    for (int hv = 0; hv < DOOM_HEAT_LEVELS; hv++) {
        int ci = (hv * 30) / 40;
        if (ci > (PAL_SIZE - 1)) {
            ci = PAL_SIZE - 1;
        }

        uint8_t r = pal[ci][0];
        uint8_t g = pal[ci][1];
        uint8_t b = pal[ci][2];
        scale_rgb_to_ledmax(&r, &g, &b);

        s_lut[pal_idx][hv] = (rgb8_t){r, g, b};
    }
    s_lut_ready[pal_idx] = 1;
}

static void build_fire_led_map_once(void) {
    if (s_fire_led_map_ready) {
        return;
    }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            s_fire_led_map[y * W + x] = (uint16_t)panel_led_index(x, H - 1 - y);
        }
    }
    s_fire_led_map_ready = true;
}

static void sim_task(void* arg) {
    (void)arg;

    bool key_inited = false;
    doom_fire_t fire;

    rgb_init();
    if (key_init(&s_key, KEY_GPIO_PIN, true, KEY_DEBOUNCE_MS) != ESP_OK) {
        ESP_LOGE(TAG, "key_init failed");
        goto exit_task;
    }
    key_inited = true;

    for (int i = 0; i < PALETTE_COUNT; i++) {
        build_lut((uint8_t)i);
    }
    build_fire_led_map_once();

    doom_fire_init(&fire, esp_random());
    doom_fire_reset(&fire);

    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t frame_ticks = pdMS_TO_TICKS(1000 / SIM_FPS);

    while (s_running) {
        vTaskDelayUntil(&last_wake, frame_ticks);

        gravity_xy_t g = gravity_get();
        float gx = 0.0f;
        if (g.valid) {
            gx = g.gx * 2.5f;
            if (gx < -2.5f) {
                gx = -2.5f;
            }
            if (gx > 2.5f) {
                gx = 2.5f;
            }
        }

        uint32_t t_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        doom_fire_step(&fire, gx, t_ms);

        if (key_get_press(&s_key)) {
            s_palette_idx = (uint8_t)((s_palette_idx + 1) % PALETTE_COUNT);
            if (!s_lut_ready[s_palette_idx]) {
                build_lut(s_palette_idx);
            }
            ESP_LOGD(TAG, "palette -> %u", (unsigned)s_palette_idx);
        }

        const float* heat = doom_fire_heat(&fire);
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                float v = heat[y * W + x];
                if (v < 0.0f) {
                    v = 0.0f;
                }
                if (v > (float)DOOM_HEAT_MAX) {
                    v = (float)DOOM_HEAT_MAX;
                }

                int hv = (int)(v + 0.5f);
                if (hv < 0) {
                    hv = 0;
                }
                if (hv >= DOOM_HEAT_LEVELS) {
                    hv = DOOM_HEAT_LEVELS - 1;
                }

                rgb8_t c = s_lut[s_palette_idx][hv];
                uint16_t idx = s_fire_led_map[y * W + x];
                rgb_set_fast((uint32_t)idx, c.r, c.g, c.b);
            }
        }
        rgb_show();
    }

exit_task:
    if (key_inited) {
        key_deinit(&s_key);
    }
    rgb_clear();
    rgb_show();

    s_running = false;
    s_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t fire_sim_start(int core_id, uint32_t stack_size, int priority) {
    if (s_task) {
        return ESP_ERR_INVALID_STATE;
    }

    s_running = true;
    BaseType_t ok = xTaskCreatePinnedToCore(sim_task, "fire_sim", stack_size, NULL, priority, &s_task, core_id);
    if (ok != pdPASS) {
        s_running = false;
        s_task = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t fire_sim_stop(uint32_t timeout_ms) {
    if (!s_task) {
        return ESP_OK;
    }

    s_running = false;

    TickType_t start = xTaskGetTickCount();
    TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
    while (s_task) {
        if ((xTaskGetTickCount() - start) >= timeout_ticks) {
            TaskHandle_t task = s_task;
            if (task) {
                vTaskDelete(task);
            }
            s_task = NULL;
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return ESP_OK;
}

bool fire_sim_is_running(void) {
    return s_task != NULL && s_running;
}
