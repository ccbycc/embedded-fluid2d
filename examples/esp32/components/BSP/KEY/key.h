#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

typedef struct {
    gpio_num_t pin;
    bool active_low;
    volatile uint8_t pressed;
    TickType_t last_tick;
    TickType_t debounce_ticks;
} key_t;

esp_err_t key_init(key_t* key, gpio_num_t pin, bool active_low, uint32_t debounce_ms);
esp_err_t key_deinit(key_t* key);
bool key_get_press(key_t* key);
