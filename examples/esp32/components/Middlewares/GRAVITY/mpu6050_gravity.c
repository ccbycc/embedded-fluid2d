#include "mpu6050_gravity.h"

#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/i2c.h"
#include "esp_log.h"

#include "mpu6050.h"
#include "gravity.h"

static const char *TAG = "mpu6050_g";

// ===== 你可以后面自己调方向（轴映射/符号） =====
// 这里默认：gx=acce_x, gy=acce_y（单位尽量保持为 g）
#define GX_FROM_AX 1
#define GY_FROM_AY 1
#define GX_SIGN    (-1.0f)
#define GY_SIGN    (-1.0f)

// ===== I2C 配置（按你官方例子）=====
#define I2C_MASTER_SCL_IO      4
#define I2C_MASTER_SDA_IO      5
#define I2C_MASTER_NUM         I2C_NUM_0
#define I2C_MASTER_FREQ_HZ     400000   // 400k 比 100k 更顺
#define I2C_MASTER_TIMEOUT_MS  1000

// ===== 任务参数 =====
#define MPU_TASK_HZ            100      // 100Hz 采样
#define MPU_LPF_ALPHA          0.20f    // 低通滤波，越大越灵敏（0..1）
#define G_CLAMP                1.5f     // 防抖：限制输出范围

static mpu6050_handle_t s_mpu = NULL;
static bool s_i2c_inited = false;

static esp_err_t i2c_bus_init_once(void)
{
    if (s_i2c_inited) return ESP_OK;

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
    if (ret != ESP_OK) return ret;

    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) return ret;

    s_i2c_inited = true;
    return ESP_OK;
}

static inline float clampf_fast(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

// 有些库 acce 输出是 g，有些是 m/s^2；这里做一个“粗判别”自动归一到 g
static inline void normalize_to_g(float *ax, float *ay, float *az)
{
    float mag = sqrtf((*ax)*(*ax) + (*ay)*(*ay) + (*az)*(*az));
    // 如果量级像 9.8 左右，认为是 m/s^2
    if (mag > 5.0f) {
        const float inv_g = 1.0f / 9.80665f;
        *ax *= inv_g;
        *ay *= inv_g;
        *az *= inv_g;
    }
}

static void mpu_task(void *arg)
{
    (void)arg;

    float gx_f = 0.0f, gy_f = 0.0f;

    while (1) {
        if (!s_mpu) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        mpu6050_acce_value_t acce;
        esp_err_t ret = mpu6050_get_acce(s_mpu, &acce);
        if (ret == ESP_OK) {
            float ax = acce.acce_x;
            float ay = acce.acce_y;
            float az = acce.acce_z;

            normalize_to_g(&ax, &ay, &az);

            float gx = 0.0f, gy = 0.0f;

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

            // 低通滤波，抑制抖动
            gx_f = (1.0f - MPU_LPF_ALPHA) * gx_f + MPU_LPF_ALPHA * gx;
            gy_f = (1.0f - MPU_LPF_ALPHA) * gy_f + MPU_LPF_ALPHA * gy;

            gravity_set(gx_f, gy_f);
        } else {
            ESP_LOGW(TAG, "mpu6050_get_acce failed: %s", esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(1000 / MPU_TASK_HZ));
    }
}

esp_err_t mpu6050_gravity_start(void)
{
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

    // 量程按你例子：4G、500DPS
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

    ESP_LOGI(TAG, "mpu6050 started (I2C %d, SDA=%d SCL=%d)",
             I2C_MASTER_NUM, I2C_MASTER_SDA_IO, I2C_MASTER_SCL_IO);

    xTaskCreatePinnedToCore(mpu_task, "mpu6050_task", 4096, NULL, 6, NULL, 0);
    return ESP_OK;
}
