#include "gravity.h"

#include <math.h>

#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "mpu6050.h"

static const char* TAG = "gravity";

static float s_gx = 0.0f;
static float s_gy = 0.0f;
static bool s_valid = false;
static portMUX_TYPE s_gravity_mux = portMUX_INITIALIZER_UNLOCKED;

static mpu6050_handle_t s_mpu = NULL;
static bool s_i2c_inited = false;
static TaskHandle_t s_sensor_task = NULL;

#define GX_FROM_AX 1
#define GY_FROM_AY 1
#define GX_SIGN (-1.0f)
#define GY_SIGN (-1.0f)

#define I2C_MASTER_SCL_IO 4
#define I2C_MASTER_SDA_IO 5
#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_MASTER_FREQ_HZ 400000

#define SENSOR_TASK_HZ 50
#define SENSOR_LPF_ALPHA 0.20f
#define G_CLAMP 1.5f

static inline float clampf_fast(float x, float lo, float hi) {
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static inline void normalize_to_g(float* ax, float* ay, float* az) {
    float mag = sqrtf((*ax) * (*ax) + (*ay) * (*ay) + (*az) * (*az));
    if (mag > 5.0f) {
        const float inv_g = 1.0f / 9.80665f;
        *ax *= inv_g;
        *ay *= inv_g;
        *az *= inv_g;
    }
}

static esp_err_t i2c_bus_init_once(void) {
    if (s_i2c_inited) {
        return ESP_OK;
    }

    i2c_config_t conf = {0};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = (gpio_num_t)I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = (gpio_num_t)I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;

#ifdef I2C_SCLK_SRC_FLAG_FOR_NOMAL
    conf.clk_flags = I2C_SCLK_SRC_FLAG_FOR_NOMAL;
#else
    conf.clk_flags = 0;
#endif

    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        return ret;
    }

    s_i2c_inited = true;
    return ESP_OK;
}

static void gravity_sensor_task(void* arg) {
    (void)arg;

    float gx_f = 0.0f;
    float gy_f = 0.0f;

    while (1) {
        mpu6050_acce_value_t acce;
        esp_err_t ret = mpu6050_get_acce(s_mpu, &acce);
        if (ret == ESP_OK) {
            float ax = acce.acce_x;
            float ay = acce.acce_y;
            float az = acce.acce_z;

            normalize_to_g(&ax, &ay, &az);

            float gx = 0.0f;
            float gy = 0.0f;

#if GX_FROM_AX
            gx = ax;
#else
            gx = ay;
#endif

#if GY_FROM_AY
            gy = ay;
#else
            gy = ax;
#endif

            gx *= GX_SIGN;
            gy *= GY_SIGN;

            gx = clampf_fast(gx, -G_CLAMP, G_CLAMP);
            gy = clampf_fast(gy, -G_CLAMP, G_CLAMP);

            gx_f = (1.0f - SENSOR_LPF_ALPHA) * gx_f + SENSOR_LPF_ALPHA * gx;
            gy_f = (1.0f - SENSOR_LPF_ALPHA) * gy_f + SENSOR_LPF_ALPHA * gy;

            gravity_set(gx_f, gy_f);
        } else {
            ESP_LOGW(TAG, "mpu6050_get_acce failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / SENSOR_TASK_HZ));
    }
}

void gravity_init(void) {
    portENTER_CRITICAL(&s_gravity_mux);
    s_gx = 0.0f;
    s_gy = 0.0f;
    s_valid = false;
    portEXIT_CRITICAL(&s_gravity_mux);
}

void gravity_set(float gx, float gy) {
    portENTER_CRITICAL(&s_gravity_mux);
    s_gx = gx;
    s_gy = gy;
    s_valid = true;
    portEXIT_CRITICAL(&s_gravity_mux);
}

gravity_xy_t gravity_get(void) {
    gravity_xy_t out;

    portENTER_CRITICAL(&s_gravity_mux);
    out.gx = s_gx;
    out.gy = s_gy;
    out.valid = s_valid;
    portEXIT_CRITICAL(&s_gravity_mux);

    return out;
}

bool gravity_is_valid(void) {
    bool v;

    portENTER_CRITICAL(&s_gravity_mux);
    v = s_valid;
    portEXIT_CRITICAL(&s_gravity_mux);

    return v;
}

esp_err_t gravity_sensor_start(void) {
    if (s_sensor_task) {
        return ESP_OK;
    }

    esp_err_t ret = i2c_bus_init_once();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_mpu = mpu6050_create(I2C_MASTER_NUM, MPU6050_I2C_ADDRESS);
    if (!s_mpu) {
        ESP_LOGE(TAG, "mpu6050_create failed");
        return ESP_FAIL;
    }

    ret = mpu6050_config(s_mpu, ACCE_FS_4G, GYRO_FS_500DPS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mpu6050_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mpu6050_wake_up(s_mpu);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mpu6050_wake_up failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "mpu6050 started (I2C %d, SDA=%d SCL=%d)", I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    BaseType_t ok = xTaskCreatePinnedToCore(gravity_sensor_task, "gravity_task", 4096, NULL, 6, &s_sensor_task, 0);
    if (ok != pdPASS) {
        s_sensor_task = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}
