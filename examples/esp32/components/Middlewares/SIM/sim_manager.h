#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SIM_MODE_NONE = -1,
    SIM_MODE_WATER = 0,
    SIM_MODE_FIRE = 1,
} sim_mode_t;

typedef struct {
    int core_id;
    uint32_t stack_size;
    int priority;
    uint32_t stop_timeout_ms;
} sim_runtime_config_t;

esp_err_t sim_manager_init(const sim_runtime_config_t* cfg);
esp_err_t sim_manager_start(sim_mode_t mode);
esp_err_t sim_manager_switch(sim_mode_t mode);
esp_err_t sim_manager_stop(void);
sim_mode_t sim_manager_current(void);

#ifdef __cplusplus
}
#endif
