# FLIP/PIC Core

这里是平台无关 C++11 2D FLIP/PIC 核心的目标位置。

核心最终只负责模拟状态和求解流程，不依赖 ESP-IDF、FreeRTOS、LED、IMU 或平台内存分配器。当前 ESP32 原始实现仍保留在 `examples/esp32/`，在完成接口和内存边界确认前不直接搬入本目录。

计划中的基本布局：

```text
fluid2d/
├── include/fluid2d/   # 公共 C++11 接口
├── src/               # FLIP/PIC 实现
└── tests/             # 主机侧确定性回归
```
