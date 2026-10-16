# embedded-fluid2d

[English](README.md) | [中文](README_CN.md)

A portable C++11 2D fluid and fire simulation core for low-cost, resource-constrained embedded platforms.

This repository evolved from a gravity-controlled water lamp built with an ESP32-S3, a 16x16 WS2812 LED matrix and an MPU6050. The project is now centered on hardware-independent algorithm libraries, while the original firmware and LED panel remain the first reference applications.

Target platforms include ESP32, STM32, other Cortex-M MCUs, Chinese MCUs, and host-based simulation environments such as Linux and Windows.

## Author's Note

This repository started as a small effects engine for an ESP32 and a 16x16 RGB LED panel, focused on gravity-controlled water waves. A fire effect was added later. The project is intentionally kept readable and easy to modify, so people can reproduce the hardware and firmware or build their own effects on top of it.

As development continued, the FLIP/PIC fluid and DOOM Fire algorithms were organized into standalone C++11 cores. The original ESP32-S3, WS2812 and MPU6050 project is still included as a reference. Developers working with STM32, other MCUs or PC simulation can use only the algorithm layer and adjust the grid, particle count and iteration count to match their available RAM and compute budget.

If this project is useful to you, please consider starring it. Issues, pull requests and demonstrations of your own builds or ports are welcome.

## Current Modules

### fluid2d

A platform-independent C++11 2D FLIP/PIC fluid core responsible for:

- Particle positions and velocities;
- Grid velocity, density and pressure;
- Particle separation and boundary collisions;
- Particle-to-grid and grid-to-particle transfers;
- PIC/FLIP blending and incompressibility solving.

The core does not depend on ESP-IDF, FreeRTOS, LEDs, IMUs, GPIO, I2C, platform timers or platform memory allocators.

- Overview: [`fluid2d/README.md`](fluid2d/README.md)
- Migration guide: [`docs/迁移教程.md`](docs/迁移教程.md)

### doom_fire

A platform-independent C++11 DOOM-style fire core responsible for:

- Heat propagation and random cooling;
- Horizontal gravity-based drift;
- Fire-source breathing, motion and sparks;
- Output of a raw heat grid.

The core does not depend on ESP-IDF, FreeRTOS, esp-dsp, LEDs, IMUs, platform timers or platform random-number APIs.

- Overview: [`doom_fire/README.md`](doom_fire/README.md)
- Migration guide: [`docs/火焰迁移教程.md`](docs/火焰迁移教程.md)

## Getting Started With Porting

Add the core directory you need to your project and compile it as C++11. With CMake, link the target directly:

```cmake
add_subdirectory(path/to/embedded-fluid2d/fluid2d)
target_link_libraries(your_app PRIVATE fluid2d)
```

The public FLIP/PIC header is `<fluid2d/fluid_solver.h>`. The application creates a `Config`, calls `requiredMemory(config)` to query the workspace size, and provides a static buffer or platform memory pool. Each frame, call `setGravity()` and `step(dt)`, then read the particle or density views. See the [fluid migration guide](docs/迁移教程.md) for the complete integration example and ESP-IDF/STM32 notes.

DOOM Fire can be integrated independently: change the CMake subdirectory above to `doom_fire`, link the `doom_fire` target, and include `<doom_fire/doom_fire.h>`. See the [fire migration guide](docs/火焰迁移教程.md) for the full procedure.

## Design Principles

- C++11 is the minimum language standard;
- The algorithm layer is responsible only for simulation, not display or hardware;
- The caller provides contiguous workspace; the core does not choose the memory source;
- `step()` performs no dynamic allocation after initialization;
- Grid size, particle capacity and solver quality can be adjusted to the MCU's RAM and CPU budget;
- The standard C++ scalar implementation is the behavioral reference; platform acceleration remains optional;
- The project does not maintain separate C and C++ implementations of the same algorithm.

The platform layer provides gravity or sensor input, time, workspace, display output, RTOS tasks and hardware initialization.

## Quick Usage

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

The main parameters affecting resource use and visual behavior are:

| Parameter | Main purpose |
| --- | --- |
| `grid_width`, `grid_height` | Grid dimensions; affect RAM and pressure-solver cost |
| `max_particles` | Maximum particle capacity; mainly affects RAM |
| `pressure_iterations` | Pressure iterations per step; mainly affects CPU time |
| `particle_push_iterations` | Particle-separation iterations; mainly affects CPU time |
| `flip_ratio` | PIC/FLIP blend ratio |
| `initial_fill_width`, `initial_fill_height` | Initial fluid fill region |
| `particle_radius_factor` | Particle size and initial particle count |
| `gravity_scale` | Gravity input scaling |

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

The main parameters are `width`, `height`, `decay`, `intensity` and `max_heat`.

## Host Build And Tests

The two cores can be built independently:

```sh
cmake -S fluid2d -B build/fluid2d
cmake --build build/fluid2d
ctest --test-dir build/fluid2d --output-on-failure

cmake -S doom_fire -B build/doom_fire
cmake --build build/doom_fire
ctest --test-dir build/doom_fire --output-on-failure
```

Current verification includes:

- Strict C++11, Release and `-fno-exceptions -fno-rtti` builds;
- ASan/UBSan;
- Configuration and workspace boundary checks;
- Deterministic regression tests with fixed inputs;
- Golden-metric regression based on the frozen ESP32 C implementation.

After 30 frames with the default FLIP/PIC configuration, the tests check aggregate particle position, velocity and density metrics together with rest density. After 120 frames with the default DOOM Fire configuration, the tests check aggregate heat-grid metrics and determinism for identical inputs. The golden values come from the frozen legacy implementation; the tests do not launch the old C solver for a live, element-by-element comparison.

## ESP32 Reference Application

[`examples/esp32/`](examples/esp32/) contains the currently buildable ESP-IDF water-lamp and fire firmware, including:

- MPU6050 gravity input;
- FreeRTOS simulation tasks;
- WS2812 LED output;
- Color palettes and brightness mapping;
- Button-based effect and color switching;
- The original C and esp-dsp algorithm implementations.

The ESP32 reference example keeps its original behavior and has not yet been switched to the new C++11 cores. Future integration should add only a lightweight C++ bridge rather than duplicate the algorithms.

## Hardware Reproduction

1. Prepare the parts listed in [`examples/hardware/购置清单.md`](examples/hardware/购置清单.md);
2. Build or connect the LED panel using the materials in [`examples/hardware/`](examples/hardware/);
3. Enter `examples/esp32/` and build and flash the project with ESP-IDF;
4. If you do not need to build the firmware yourself, see the simple prebuilt firmware in [`prebuilt/esp32/`](prebuilt/esp32/).

Power and flashing precautions:

- Complete all wiring while the hardware is powered off;
- Disconnect the computer USB cable after flashing, then connect the external power supply;
- Do not power the board from both the external supply and the computer USB port while it is running;
- Check for shorts, reversed polarity and sufficient power-supply current before powering on.

## Demonstrations

- Water lamp: [Turning an LED panel into flowing water with ESP32 and FLIP](https://www.bilibili.com/video/BV1BoFoz6EGF/)
- Fire effect: [Creating a flickering fire effect with the DOOM algorithm](https://www.bilibili.com/video/BV124ZBBhEyS/)
- LED panel hardware: [JLC Open Source Hardware platform](https://oshwhub.com/ccbaw123/deng-ban-flip)

## Repository Structure

```text
embedded-fluid2d/
├── fluid2d/            # Platform-independent C++11 FLIP/PIC core
├── doom_fire/          # Platform-independent C++11 fire core
├── examples/
│   ├── esp32/          # ESP-IDF reference application and frozen legacy implementation
│   └── hardware/       # ESP32 LED matrix reference hardware
├── prebuilt/
│   └── esp32/          # Simple prebuilt firmware
└── docs/               # Migration guides and hardware documentation
```

## Project Status

Completed:

- Platform-independent C++11 FLIP/PIC core;
- Platform-independent C++11 DOOM Fire core;
- Caller-provided workspace and zero real-time dynamic allocation;
- Host tests and regression checks against the legacy implementation;
- Fluid and fire migration guides.

## Changelog

### 2026-09-18: Portable Core, Phase 2

- Completed the platform-independent C++11 FLIP/PIC core;
- Completed the platform-independent C++11 DOOM Fire core;
- Both cores now use caller-provided workspace and perform no dynamic allocation in the real-time loop;
- Added configuration boundary checks, deterministic tests and golden regression metrics from the legacy C implementations;
- Verified strict C++11, Release, ASan/UBSan and no-exceptions/no-RTTI builds;
- Added fluid and fire migration guides;
- Kept the ESP32 reference example frozen and unchanged in behavior.

### v1.2.0: ESP32 Effects Firmware

- Added the DOOM Fire simulation;
- Added multiple fire color palettes;
- Added `sim_manager` for managing the water and fire effects;
- Added button-based simulator and color switching;
- Centralized LED panel dimensions and serpentine mapping in `panel_config.h`;
- Provided esp-dsp and without-dsp algorithm variants;
- Consolidated the gravity-input modules.

### v1.0.x: Water-Lamp Prototype

- Completed the ESP32, MPU6050 and WS2812 gravity-controlled water lamp;
- Added serpentine LED-panel mapping;
- Added an LED soldering test program.

## License

- Firmware and code: MIT License, see [`examples/esp32/LICENSE`](examples/esp32/LICENSE);
- Hardware: CC BY-NC 3.0, see [`examples/hardware/LICENSE`](examples/hardware/LICENSE);
- Documentation: CC BY-NC 3.0, see [`docs/LICENSE`](docs/LICENSE);
- Third-party components remain under their respective licenses.

## Attribution

Parts of the FLIP/PIC implementation were adapted and ported from work by Matthias Müller of Ten Minute Physics, originally released under the MIT License.

- Original materials: <https://github.com/matthias-research/pages>
- Video explanation: <https://www.youtube.com/watch?v=XmzBREkK8kY>
