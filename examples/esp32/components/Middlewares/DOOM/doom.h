#pragma once

#include <stdint.h>

#include "panel_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_W PANEL_WIDTH
#define DOOM_H PANEL_HEIGHT
#define DOOM_N (DOOM_W * DOOM_H)

#define DOOM_HEAT_MAX 80
#define DOOM_HEAT_LEVELS (DOOM_HEAT_MAX + 1)

typedef struct {
    float decay;
    float intensity;
    uint32_t rng;
    float heat[DOOM_N];
    float next[DOOM_N];
} doom_fire_t;

void doom_fire_init(doom_fire_t* f, uint32_t seed);
void doom_fire_reset(doom_fire_t* f);
void doom_fire_step(doom_fire_t* f, float gravity_x, uint32_t t_ms);
const float* doom_fire_heat(const doom_fire_t* f);

#ifdef __cplusplus
}
#endif
