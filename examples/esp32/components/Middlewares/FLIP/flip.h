/*
Portions of this file are adapted/ported from work by:
  Copyright 2022 Matthias Müller - Ten Minute Physics
  www.youtube.com/c/TenMinutePhysics
  www.matthiasMueller.info/tenMinutePhysics

Licensed under the MIT License. See examples/esp32/LICENSE for the full text.

Modifications/port to C:
  Copyright 2026 cccAboy
*/
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlipFluid FlipFluid;

FlipFluid* flip_create(float sim_w, float sim_h, int visible_res,
                       float fill_ratio);
void flip_destroy(FlipFluid* f);

void flip_step(FlipFluid* f, float dt, float gx, float gy);

void flip_get_led_grid(const FlipFluid* f, float* out_grid);

void flip_set_gravity_scale(FlipFluid* f, float gravity_scale);
void flip_set_solver_quality(FlipFluid* f, int push_iters, int pressure_iters,
                             float flip_ratio);

#ifdef __cplusplus
}
#endif
