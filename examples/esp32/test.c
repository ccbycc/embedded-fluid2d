//替换main.c即可测试灯板
#include <stdio.h>
#include <stdarg.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "rgb.h"

void app_main(void)
{
    const uint8_t led_level = 20;
    rgb_init();

    rgb_clear();
    for (uint32_t i = 0; i < 256; ++i) {
        rgb_set(i, led_level, led_level, led_level);
    }
    rgb_show();

}
