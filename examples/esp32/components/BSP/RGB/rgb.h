#ifndef __RGB_H_
#define __RGB_H_
#include <stdint.h>
void rgb_init(void);
void rgb_deinit(void);
void rgb_set(uint32_t index,uint8_t r, uint8_t g, uint8_t b);
void rgb_set_fast(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
void rgb_clear(void);
void rgb_set_hsv(uint32_t index, uint16_t hue,uint16_t light);
void rgb_show(void);
#endif // !__RGB_H_
