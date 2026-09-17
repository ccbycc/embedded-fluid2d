#ifndef GRAVITY_H
#define GRAVITY_H

#include <stdbool.h>

#include "esp_err.h"

typedef struct {
    float gx;
    float gy;
    bool valid;
} gravity_xy_t;

void gravity_init(void);
void gravity_set(float gx, float gy);
gravity_xy_t gravity_get(void);
bool gravity_is_valid(void);

// Start gravity sensor producer task (current backend: MPU6050).
esp_err_t gravity_sensor_start(void);

#endif
