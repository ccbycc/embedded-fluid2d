#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t fire_sim_start(int core_id, uint32_t stack_size, int priority);
esp_err_t fire_sim_stop(uint32_t timeout_ms);
bool fire_sim_is_running(void);

#ifdef __cplusplus
}
#endif
