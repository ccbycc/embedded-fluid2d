#include "rgb.h"

#include "esp_err.h"
#include "esp_rom_sys.h"
#include "led_strip.h"
#include "panel_config.h"

#define RGB_GPIO_PIN 10
#define RGB_LED_COUNT PANEL_LED_COUNT
#define RGB_RMT_RES_HZ (10 * 1000 * 1000)

static led_strip_handle_t s_strip = NULL;

void rgb_init(void) {
    if (s_strip) {
        return;
    }

    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_GPIO_PIN,
        .max_leds = RGB_LED_COUNT,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags = {
            .invert_out = false,
        }};

    led_strip_rmt_config_t rmt_config = {.clk_src = RMT_CLK_SRC_DEFAULT,
                                         .resolution_hz = RGB_RMT_RES_HZ,
                                         .mem_block_symbols = 64,
                                         .flags = {
                                             .with_dma = true,
                                         }};

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip));
    ESP_ERROR_CHECK(led_strip_clear(s_strip));
    ESP_ERROR_CHECK(led_strip_refresh(s_strip));
}

void rgb_deinit(void) {
    if (!s_strip) {
        return;
    }

    led_strip_del(s_strip);
    s_strip = NULL;
}

void rgb_set(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_strip || index >= RGB_LED_COUNT) {
        return;
    }

    uint8_t m = r;
    if (g > m) {
        m = g;
    }
    if (b > m) {
        m = b;
    }

    if (m > PANEL_LED_VALUE_MAX) {
        r = (uint16_t)r * PANEL_LED_VALUE_MAX / m;
        g = (uint16_t)g * PANEL_LED_VALUE_MAX / m;
        b = (uint16_t)b * PANEL_LED_VALUE_MAX / m;
    }

    led_strip_set_pixel(s_strip, index, r, g, b);
}

void rgb_set_fast(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!s_strip || index >= RGB_LED_COUNT) {
        return;
    }
    led_strip_set_pixel(s_strip, index, r, g, b);
}

void rgb_clear(void) {
    if (!s_strip) {
        return;
    }
    led_strip_clear(s_strip);
}

void rgb_set_hsv(uint32_t index, uint16_t hue, uint16_t light) {
    if (!s_strip || index >= RGB_LED_COUNT) {
        return;
    }

    hue %= 360;
    light = (light > PANEL_LED_VALUE_MAX) ? PANEL_LED_VALUE_MAX : light;
    led_strip_set_pixel_hsv(s_strip, index, hue, 255, light);
}

void rgb_show(void) {
    if (!s_strip) {
        return;
    }

    esp_err_t err = led_strip_refresh(s_strip);
    if (err != ESP_OK) {
        led_strip_refresh(s_strip);
    }
    esp_rom_delay_us(80);
}
