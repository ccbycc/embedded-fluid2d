#include "sim_manager.h"

#include <stdbool.h>

#include "esp_log.h"
#include "fire_sim.h"
#include "water_sim.h"

static const char* TAG = "sim_manager";

static sim_runtime_config_t s_cfg = {
    .core_id = 1,
    .stack_size = 8192,
    .priority = 5,
    .stop_timeout_ms = 1000,
};

static sim_mode_t s_current = SIM_MODE_NONE;
static bool s_inited = false;

static esp_err_t start_mode(sim_mode_t mode) {
    if (mode == SIM_MODE_WATER) {
        return water_sim_start(s_cfg.core_id, s_cfg.stack_size, s_cfg.priority);
    }
    if (mode == SIM_MODE_FIRE) {
        return fire_sim_start(s_cfg.core_id, s_cfg.stack_size, s_cfg.priority);
    }
    return ESP_ERR_INVALID_ARG;
}

static esp_err_t stop_mode(sim_mode_t mode) {
    if (mode == SIM_MODE_WATER) {
        return water_sim_stop(s_cfg.stop_timeout_ms);
    }
    if (mode == SIM_MODE_FIRE) {
        return fire_sim_stop(s_cfg.stop_timeout_ms);
    }
    return ESP_OK;
}

esp_err_t sim_manager_init(const sim_runtime_config_t* cfg) {
    if (cfg) {
        s_cfg = *cfg;
    }
    s_current = SIM_MODE_NONE;
    s_inited = true;
    return ESP_OK;
}

esp_err_t sim_manager_start(sim_mode_t mode) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_current != SIM_MODE_NONE) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = start_mode(mode);
    if (err == ESP_OK) {
        s_current = mode;
    } else {
        ESP_LOGE(TAG, "start mode %d failed: %s", (int)mode, esp_err_to_name(err));
    }
    return err;
}

esp_err_t sim_manager_switch(sim_mode_t mode) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (mode == s_current) {
        return ESP_OK;
    }

    esp_err_t err = stop_mode(s_current);
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGE(TAG, "stop mode %d failed: %s", (int)s_current, esp_err_to_name(err));
        return err;
    }
    s_current = SIM_MODE_NONE;

    err = start_mode(mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "start mode %d failed: %s", (int)mode, esp_err_to_name(err));
        return err;
    }

    s_current = mode;
    return ESP_OK;
}

esp_err_t sim_manager_stop(void) {
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = stop_mode(s_current);
    s_current = SIM_MODE_NONE;
    return err;
}

sim_mode_t sim_manager_current(void) {
    return s_current;
}
