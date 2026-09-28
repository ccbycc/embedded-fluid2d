# DOOM Fire

`doom_fire` 是独立、平台无关的 C++11 火焰效果核心，不属于 FLIP/PIC 流体核心。

它只维护火焰状态并输出原始热度网格，不依赖 ESP-IDF、FreeRTOS、esp-dsp、LED、IMU 或平台随机数 API。调色板、亮度限制、传感器读取、时间获取和任务调度由应用层负责。

现有 ESP32 实现保留在 `examples/esp32/components/Middlewares/DOOM/`，作为行为参考。

## 使用

```cpp
doom_fire::Config config;
const std::size_t bytes = doom_fire::DoomFire::requiredMemory(config);
void* workspace = obtain_workspace(bytes);

doom_fire::DoomFire fire;
fire.initialize(config, workspace, bytes, random_seed);
fire.step(gravity_x, time_ms);

const doom_fire::HeatView heat = fire.heatGrid();
const float value = heat.values[y * heat.stride + x];
```

默认配置为 16×16、`decay = 3`、`intensity = 36`、`max_heat = 80`，对应旧 ESP32 实现。相同 seed、重力和时间输入会产生确定性结果。

调用方提供连续 workspace，初始化和 `step()` 都不从堆分配内存。当前算法保留原始12行火源几何，因此 `height` 不能小于12。

完整接入步骤见 [`docs/火焰迁移教程.md`](../docs/火焰迁移教程.md)。

## 构建

```sh
cmake -S doom_fire -B build/doom_fire
cmake --build build/doom_fire
ctest --test-dir build/doom_fire --output-on-failure
```
