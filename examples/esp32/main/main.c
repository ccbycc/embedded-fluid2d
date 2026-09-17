#include "esp_err.h"
#include "esp_log.h"

#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gravity.h"
#include "sim_manager.h"

static const char* TAG = "app_main";
static const sim_mode_t g_boot_mode = SIM_MODE_FIRE;

#define MODE_KEY_PIN GPIO_NUM_0
#define MODE_KEY_ACTIVE_LOW 1
#define MODE_KEY_POLL_MS 20
#define MODE_SWITCH_LONG_PRESS_MS 1000

static void mode_switch_task(void* arg) {
    (void)arg;

    bool pressed = false;
    bool switched = false;
    TickType_t press_tick = 0;

    while (1) {
        int level = gpio_get_level(MODE_KEY_PIN);
        bool down = MODE_KEY_ACTIVE_LOW ? (level == 0) : (level != 0);
        TickType_t now = xTaskGetTickCount();

        if (down) {
            if (!pressed) {
                pressed = true;
                switched = false;
                press_tick = now;
            } else if (!switched && (now - press_tick) >= pdMS_TO_TICKS(MODE_SWITCH_LONG_PRESS_MS)) {
                sim_mode_t cur = sim_manager_current();
                sim_mode_t next = (cur == SIM_MODE_FIRE) ? SIM_MODE_WATER : SIM_MODE_FIRE;
                esp_err_t err = sim_manager_switch(next);
                if (err == ESP_OK) {
                    ESP_LOGI(TAG, "mode switched: %d -> %d", (int)cur, (int)next);
                } else {
                    ESP_LOGE(TAG, "mode switch failed: %s", esp_err_to_name(err));
                }
                switched = true;
            }
        } else {
            pressed = false;
            switched = false;
        }

        vTaskDelay(pdMS_TO_TICKS(MODE_KEY_POLL_MS));
    }
}

void app_main(void) {
    gravity_init();

    esp_err_t err = gravity_sensor_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gravity_sensor_start failed: %s", esp_err_to_name(err));
        return;
    }

    sim_runtime_config_t cfg = {
        .core_id = 1,
        .stack_size = 8192,
        .priority = 5,
        .stop_timeout_ms = 1000,
    };

    err = sim_manager_init(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sim_manager_init failed: %s", esp_err_to_name(err));
        return;
    }

    err = sim_manager_start(g_boot_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "sim_manager_start failed: %s", esp_err_to_name(err));
        return;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(mode_switch_task, "mode_switch", 3072, NULL, 4, NULL, 0);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "mode switch task create failed");
    }
}
