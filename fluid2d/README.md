# fluid2d

`fluid2d` 是平台无关的 C++11 2D FLIP/PIC 核心。它只负责粒子、网格速度、密度、压力、不可压缩求解和边界碰撞，不依赖 ESP-IDF、FreeRTOS、LED、IMU 或平台内存分配器。

当前 ESP32 实现保留在 `examples/esp32/`，作为行为参考和后续适配层。

## 目录

```text
fluid2d/
├── fluid_solver.h
├── fluid_solver.cpp
├── smoke_test.cpp
└── CMakeLists.txt
```

## 使用

调用方负责提供一块连续、可写且在 solver 生命周期内保持有效的 workspace。`initialize()` 和 `step()` 不从堆分配内存。

```cpp
fluid2d::Config config;
const std::size_t bytes = fluid2d::FluidSolver::requiredMemory(config);

// 内存来源由平台决定：静态数组、内存池、PSRAM 或主机容器均可。
void* workspace = obtain_workspace(bytes);

fluid2d::FluidSolver solver;
solver.initialize(config, workspace, bytes);
solver.setGravity(gx, gy);
solver.step(dt);

const fluid2d::ParticleView particles = solver.particles();
const fluid2d::GridView density = solver.densityGrid();
```

粒子位置和速度使用交错的 `x, y` 布局。密度视图不拥有内存，已去掉一圈边界 padding；网格沿用原实现的 x-major 布局，元素索引为 `values[x * stride + y]`。核心返回原始密度和 `rest_density`，不进行归一化、gamma 或亮度映射。

默认几何配置对应旧 `flip_create(1.0, 1.0, 16, 0.6)`：180 个初始粒子，最大容量 436。当前 ESP32 应用会进一步把压力迭代从默认 `12` 调整为 `10`。

完整的平台接入步骤见 [`docs/迁移教程.md`](../docs/迁移教程.md)。

## 主机构建

```sh
cmake -S fluid2d -B build/fluid2d
cmake --build build/fluid2d
ctest --test-dir build/fluid2d --output-on-failure
```
