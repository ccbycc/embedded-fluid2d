# embedded-fluid2d

面向低算力、低内存嵌入式平台的可移植 C++11 二维流体与火焰效果核心。

本仓库由 ESP32-S3、16×16 WS2812 LED 矩阵和 MPU6050 水流灯项目演进而来。现在项目主体是与硬件无关的算法库，ESP32 固件和灯板是第一套参考应用。

目标平台包括 ESP32、STM32、其他 Cortex-M MCU、国产 MCU，以及 Linux、Windows 等主机仿真环境。

## 作者的话

这个仓库最初是我在 ESP32 + 16×16 RGB 灯板上做的一套小型效果引擎，主打重力感应水波，后来又加入了火焰效果。整体结构尽量写得新手也能看懂、能改得动，希望大家既可以直接复刻硬件和固件，也可以在这个基础上继续增加自己的效果。

随着项目继续开发，现在又把 FLIP/PIC 水流和 DOOM Fire 火焰算法整理成了独立的 C++11 核心。原来的 ESP32-S3、WS2812 和 MPU6050 工程仍然保留，想做 STM32、其他 MCU 或 PC 仿真的朋友，也可以只使用算法部分，根据自己的 RAM 和算力调整网格、粒子数和迭代次数。

如果这个项目对你有帮助，麻烦点个 Star 支持一下。也欢迎提 Issue、提交 PR，或者分享你的成品和移植结果。

## 当前模块

### fluid2d

平台无关的 C++11 2D FLIP/PIC 流体核心，负责：

- 粒子位置和速度；
- 网格速度、密度和压力；
- 粒子互斥与边界碰撞；
- particle-to-grid 和 grid-to-particle 传递；
- PIC/FLIP 混合与不可压缩压力求解。

核心不依赖 ESP-IDF、FreeRTOS、LED、IMU、GPIO、I2C、平台 timer 或平台内存分配器。

- 入口：[`fluid2d/README.md`](fluid2d/README.md)
- 迁移教程：[`docs/迁移教程.md`](docs/迁移教程.md)

### doom_fire

平台无关的 C++11 DOOM 风格火焰核心，负责：

- 热度传播与随机冷却；
- 水平重力偏移；
- 火源呼吸、摆动和火花；
- 输出原始热度网格。

核心不依赖 ESP-IDF、FreeRTOS、esp-dsp、LED、IMU、平台 timer 或平台随机数 API。

- 入口：[`doom_fire/README.md`](doom_fire/README.md)
- 迁移教程：[`docs/火焰迁移教程.md`](docs/火焰迁移教程.md)

## 开始移植

把需要的核心目录加入你的工程，并用 C++11 编译。使用 CMake 时可以直接链接目标：

```cmake
add_subdirectory(path/to/embedded-fluid2d/fluid2d)
target_link_libraries(your_app PRIVATE fluid2d)
```

FLIP/PIC 的公开头文件是 `<fluid2d/fluid_solver.h>`。应用层创建 `Config`，用 `requiredMemory(config)` 查询 workspace 大小，再提供静态缓冲区或平台内存池；每帧调用 `setGravity()` 和 `step(dt)`，最后读取粒子或密度视图。完整接入示例和 ESP-IDF/STM32 注意事项见[流体迁移教程](docs/迁移教程.md)。

DOOM Fire 可单独接入：将上面的 CMake 子目录改为 `doom_fire`，链接目标改为 `doom_fire`，头文件使用 `<doom_fire/doom_fire.h>`。完整步骤见[火焰迁移教程](docs/火焰迁移教程.md)。

## 设计原则

- 最低语言标准为 C++11；
- 算法层只负责模拟，不负责显示和硬件；
- 调用方提供连续 workspace，核心不决定内存来源；
- 初始化完成后，实时 `step()` 中不进行动态分配；
- 网格、粒子容量和求解质量可以按 MCU 的 RAM、CPU 预算调整；
- 标准 C++ 标量实现是行为基准，平台加速只能作为可选实现；
- 不同时维护两套独立的 C 和 C++ 算法。

平台层负责重力或传感器输入、时间、workspace、显示输出、RTOS task 和硬件初始化。

## 快速使用

### FLIP/PIC

```cpp
#include <fluid2d/fluid_solver.h>

fluid2d::Config config;
const std::size_t bytes = fluid2d::FluidSolver::requiredMemory(config);
void* workspace = obtain_workspace(bytes);

fluid2d::FluidSolver solver;
solver.initialize(config, workspace, bytes);
solver.setGravity(gx, gy);
solver.step(dt);

const fluid2d::ParticleView particles = solver.particles();
const fluid2d::GridView density = solver.densityGrid();
```

影响资源和效果的主要参数：

| 参数 | 主要作用 |
| --- | --- |
| `grid_width`, `grid_height` | 网格尺寸，影响 RAM 和压力求解开销 |
| `max_particles` | 最大粒子容量，主要影响 RAM |
| `pressure_iterations` | 压力迭代次数，主要影响 CPU |
| `particle_push_iterations` | 粒子互斥迭代次数，主要影响 CPU |
| `flip_ratio` | PIC/FLIP 混合比例 |
| `initial_fill_width`, `initial_fill_height` | 初始流体填充范围 |
| `particle_radius_factor` | 粒子尺寸和初始粒子数量 |
| `gravity_scale` | 重力输入缩放 |

### DOOM Fire

```cpp
#include <doom_fire/doom_fire.h>

doom_fire::Config config;
const std::size_t bytes = doom_fire::DoomFire::requiredMemory(config);
void* workspace = obtain_workspace(bytes);

doom_fire::DoomFire fire;
fire.initialize(config, workspace, bytes, random_seed);
fire.step(gravity_x, time_ms);

const doom_fire::HeatView heat = fire.heatGrid();
```

主要参数为 `width`、`height`、`decay`、`intensity` 和 `max_heat`。

## 主机构建与测试

两个核心分别独立构建：

```sh
cmake -S fluid2d -B build/fluid2d
cmake --build build/fluid2d
ctest --test-dir build/fluid2d --output-on-failure

cmake -S doom_fire -B build/doom_fire
cmake --build build/doom_fire
ctest --test-dir build/doom_fire --output-on-failure
```

当前验证包括：

- 严格 C++11、Release 和 `-fno-exceptions -fno-rtti` 构建；
- ASan/UBSan；
- 配置和 workspace 边界；
- 固定输入的确定性回归；
- 基于冻结 ESP32 C 实现记录的 golden 指标回归。

默认 FLIP/PIC 配置运行30帧后，测试检查粒子位置、速度、密度的汇总指标及静止密度；默认 DOOM Fire 配置运行120帧后，测试检查热度网格汇总指标和相同输入下的确定性。golden 值来自冻结的旧实现，不是在测试运行时逐项启动旧 C 算法进行比较。

## ESP32 参考应用

[`examples/esp32/`](examples/esp32/) 保留当前可编译的 ESP-IDF 水流灯和火焰固件，包含：

- MPU6050 重力输入；
- FreeRTOS 模拟任务；
- WS2812 LED 输出；
- 调色板和亮度映射；
- 按键切换效果与颜色；
- 原始 C/esp-dsp 算法实现。

当前 ESP32 reference example 仍保持原行为，尚未切换为调用新的 C++11 核心。后续适配只应增加轻量 C++ bridge，不再复制算法。

## 硬件复刻

1. 根据 [`examples/hardware/购置清单.md`](examples/hardware/购置清单.md) 准备器件；
2. 使用 [`examples/hardware/`](examples/hardware/) 中的资料制作或连接灯板；
3. 进入 `examples/esp32/` 使用 ESP-IDF 编译和烧录；
4. 不需要自行编译时，可查看 [`prebuilt/esp32/`](prebuilt/esp32/) 中的简易固件。

供电和烧录注意事项：

- 所有硬件接线必须在断电状态下完成；
- 烧录完成后先断开电脑 USB，再连接外部电源；
- 外部供电运行时，避免同时通过电脑 USB 给开发板供电；
- 上电前检查短路、反接以及电源模块的电流能力。

## 演示

- 水流灯：[用 ESP32 和 FLIP 把灯板变成会流动的水](https://www.bilibili.com/video/BV1BoFoz6EGF/)
- 火焰效果：[用 DOOM 算法实现跳动火花](https://www.bilibili.com/video/BV124ZBBhEyS/)
- 灯板硬件：[嘉立创开源硬件平台](https://oshwhub.com/ccbaw123/deng-ban-flip)

## 仓库结构

```text
embedded-fluid2d/
├── fluid2d/            # 平台无关 C++11 FLIP/PIC 核心
├── doom_fire/          # 平台无关 C++11 火焰核心
├── examples/
│   ├── esp32/          # ESP-IDF 参考应用和冻结的原始实现
│   └── hardware/       # ESP32 LED 矩阵参考硬件
├── prebuilt/
│   └── esp32/          # 简易预编译固件
└── docs/               # 迁移教程和硬件资料
```

## 项目状态

已完成：

- FLIP/PIC 平台无关 C++11 核心；
- DOOM Fire 平台无关 C++11 核心；
- 外部 workspace 和零实时动态分配；
- 主机测试与旧实现回归对照；
- 流体和火焰迁移教程。

## 更新日志

### 2026-09-18：可移植核心第二阶段

- 完成平台无关 C++11 FLIP/PIC 核心；
- 完成平台无关 C++11 DOOM Fire 核心；
- 两个核心均改为调用方提供 workspace，实时循环零动态分配；
- 增加配置边界检查、确定性测试和旧 C 实现 golden 回归；
- 验证严格 C++11、Release、ASan/UBSan 和关闭异常/RTTI构建；
- 增加流体与火焰迁移教程；
- ESP32 reference example 保持冻结，尚未切换到新核心。

### v1.2.0：ESP32 效果固件

- 增加 DOOM Fire 火焰模拟；
- 增加多套火焰调色板；
- 使用 `sim_manager` 管理水流和火焰效果；
- 支持按键切换模拟器与颜色；
- 将灯板宽高和蛇形走线配置集中到 `panel_config.h`；
- 提供 esp-dsp 与 without-dsps 算法版本；
- 整理重力输入模块。

### v1.0.x：水流灯原型

- 完成 ESP32、MPU6050 和 WS2812 重力感应水流灯；
- 增加蛇形灯板走线映射；
- 增加 LED 焊接测试程序。

## License

- Firmware and code: MIT License，见 [`examples/esp32/LICENSE`](examples/esp32/LICENSE)；
- Hardware: CC BY-NC 3.0，见 [`examples/hardware/LICENSE`](examples/hardware/LICENSE)；
- Documentation: CC BY-NC 3.0，见 [`docs/LICENSE`](docs/LICENSE)；
- 第三方组件继续遵循各自许可证。

## Attribution

部分 FLIP/PIC 实现由 Matthias Müller 的 Ten Minute Physics 工作移植而来，原始项目采用 MIT License。

- 原始资料：<https://github.com/matthias-research/pages>
- 视频讲解：<https://www.youtube.com/watch?v=XmzBREkK8kY>
