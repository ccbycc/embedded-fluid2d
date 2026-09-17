#include "key.h"

#include "driver/gpio.h"
#include "esp_check.h"

static bool s_isr_service_installed = false;

static void IRAM_ATTR key_isr(void* arg) {
    key_t* key = (key_t*)arg;
    TickType_t now = xTaskGetTickCountFromISR();

    if ((now - key->last_tick) < key->debounce_ticks) {
        return;
    }

    key->last_tick = now;
    key->pressed = 1;
}

esp_err_t key_init(key_t* key, gpio_num_t pin, bool active_low, uint32_t debounce_ms) {
    ESP_RETURN_ON_FALSE(key != NULL, ESP_ERR_INVALID_ARG, "key", "key is NULL");

    key->pin = pin;
    key->active_low = active_low;
    key->pressed = 0;
    key->last_tick = 0;
    key->debounce_ticks = pdMS_TO_TICKS(debounce_ms);

    gpio_config_t io = {0};
    io.pin_bit_mask = 1ULL << pin;
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io.pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE;
    io.intr_type = active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE;
    ESP_RETURN_ON_ERROR(gpio_config(&io), "key", "gpio_config failed");

    if (!s_isr_service_installed) {
        esp_err_t err = gpio_install_isr_service(0);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            return err;
        }
        s_isr_service_installed = true;
    }

    ESP_RETURN_ON_ERROR(gpio_isr_handler_add(pin, key_isr, key), "key", "gpio_isr_handler_add failed");
    return ESP_OK;
}

esp_err_t key_deinit(key_t* key) {
    ESP_RETURN_ON_FALSE(key != NULL, ESP_ERR_INVALID_ARG, "key", "key is NULL");

    esp_err_t err = gpio_isr_handler_remove(key->pin);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE && err != ESP_ERR_NOT_FOUND) {
        return err;
    }

    key->pressed = 0;
    return ESP_OK;
}

bool key_get_press(key_t* key) {
    if (!key) {
        return false;
    }
    if (key->pressed) {
        key->pressed = 0;
        return true;
    }
    return false;
}
