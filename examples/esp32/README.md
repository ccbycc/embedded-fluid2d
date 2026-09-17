# 1. 目录结构

```
components/
├─ BSP/                 // 硬件驱动
│  ├─ KEY/              // 按键
│  ├─ RGB/              // 灯板驱动
│  └─ mpu6050/          // 重力传感器
│
├─ Middlewares/         // 算法与效果
│  ├─ FLIP/             // 2D 流体
│  ├─ DOOM/             // DOOM 火焰
│  ├─ GRAVITY/          // 重力方向
│  └─ SIM/              // 各类效果任务
│
main/
└─ app_main.c           // 入口
```

------

# 2. 模块接口

------

## 2.1 FLIP（流体）

**功能**：2D 粒子流体仿真 → 输出亮度网格

### 创建 / 销毁

```c
FlipFluid* flip_create(float sim_w, float sim_h, int visible_res, float fill_ratio);
void flip_destroy(FlipFluid* f);
```

- 仿真分辨率 = `visible_res + 2`
- `fill_ratio` 初始水位

------

### 重力强度

```c
void flip_set_gravity_scale(FlipFluid* f, float gravity_scale);
```

示例：

- 地球：`9.81f`
- 月球：`1.62f`

------

### 推进一步

```c
void flip_step(FlipFluid* f, float dt, float gx, float gy);
```

内部流程：

1. 粒子积分
2. 粒子互斥
3. 边界碰撞
4. 粒子→网格速度 (FLIP ratio=0.9)
5. 密度计算
6. 压力投影 (20 iter)
7. 网格→粒子

------

### 获取显示网格

```c
void flip_get_led_grid(const FlipFluid* f, float* out_grid);
```

- 输出尺寸：`W * H`
- 索引顺序：`grid[x * H + y]`
- 内部含：
	- 密度归一
	- clamp
	- gamma
	- 映射到 0~20

------

## 2.2 DOOM 火焰（doom.c）

**功能**：经典 DOOM Fire + 重力横向偏移

------

### 初始化

```c
void doom_fire_init(doom_fire_t* f, uint32_t seed);
void doom_fire_reset(doom_fire_t* f);
```

------

### 推进一步

```c
void doom_fire_step(doom_fire_t* f, float gravity_x, uint32_t t_ms);
```

内部包含：

- propagate（向上扩散 + 冷却）
- ignite（底部火源）
- 随机火花
- 横向重力漂移

------

### 获取热量场

```c
const float* doom_fire_heat(const doom_fire_t* f);
```

- 返回 `W*H` 热量数组
- 数值范围：`0 ~ DOOM_HEAT_MAX`

------

## 2.3 water_sim（流体任务）

- 读取重力
- 调用 FLIP
- 映射调色板
- 输出 LED

关键：

- `SIM_FPS`
- `LED_VAL_MAX_I`
- 蛇形走线映射

------

## 2.4 fire_sim（火焰任务）

- 30 FPS
- 重力控制火焰偏移
- 3 套调色板
- LUT 预计算
- 按键切换调色板

核心流程：

```c
doom_fire_step(...)
heat -> clamp
heat -> hv
hv -> LUT -> rgb
rgb_show()
```

------

# 3. 常用调参

## FLIP 手感

| 参数          | 作用         |
| ------------- | ------------ |
| gravity_scale | 重力强度     |
| flip_ratio    | 细节保留     |
| num_iters     | 不可压缩强度 |
| push_iters    | 粒子硬度     |

------

## DOOM 火焰

| 参数         | 位置     |
| ------------ | -------- |
| f->decay     | 冷却速度 |
| f->intensity | 火源强度 |
| gx           | 横向漂移 |
| SIM_FPS      | 流畅度   |

------

# 4. 注意事项

- 掉帧优先降 FPS 或减迭代次数
