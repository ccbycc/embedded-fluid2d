# Portable 2D FLIP/PIC

本项目正在从 ESP32 水流灯原型演进为面向资源受限平台的可移植 C++11 2D FLIP/PIC 流体模拟核心。

ESP32-S3、16×16 WS2812 和 MPU6050 是当前参考应用，不是算法核心的运行前提。现有 ESP32 工程暂时保持原行为，后续将逐步改为调用平台无关核心。

- 演示视频：【用esp32和flip把灯板变成会流动的水】 https://www.bilibili.com/video/BV1BoFoz6EGF/?share_source=copy_web&vd_source=7e1d93ad9ac103c6c0653b43edfdaebd
- 演示视频2：【用doom算法实现跳动火花】 https://www.bilibili.com/video/BV124ZBBhEyS/?share_source=copy_web&vd_source=7e1d93ad9ac103c6c0653b43edfdaebd
- 代码仓库：<https://github.com/ccbycc/embedded-fluid2d>
- 嘉立创链接：https://oshwhub.com/ccbaw123/deng-ban-flip

---

## 作者的话：

这个仓库是我在 ESP32 + 16×16 RGB 灯板上做的一套小型“效果引擎”：目前主打 **重力感应水波（MPU6050）**，整体结构尽量写得 **新手也能看懂、能改得动**，后续也会继续加入更多效果（比如火焰、爱心等），把它做成一个“拿来就能玩”的小项目。

如果这个项目对你有帮助——哪怕只是让你少踩一个坑——**麻烦右上角点个 Star ⭐** 支持一下！
 Star 就是我继续更新和维护的最大动力，也欢迎：

- 提 Issue：遇到问题/想要的功能都可以说
- PR：一起把效果做得更酷更顺滑
- 分享：把你的成品图/视频贴出来，我超爱看！

再次感谢大家，Let’s make it glow ✨

---

## 快速上手

1. 按照docs文件夹里面的购置清单购买器件

	如果在网购平台购买成品灯板，下面2，3步可以跳过。

2. 在嘉立创打板（每月能免费打板两次），打板文件可从嘉立创链接或 `examples/hardware/` 获取

3. 焊接（器件较小，需要加热台和焊接烙铁）并将MPU6050黏贴在灯板背后

4. 软件烧录：进入 `examples/esp32/`，编译并烧录 ESP-IDF 工程

  （如不知道如何操作，推荐参考正点原子ESP32快速入门（IDF版），从0操作，到第9讲即可使用）

5. 按照 `examples/hardware/README.md` 中的接线图连接硬件

6. 上电，恭喜你已经完成项目

**注意：**

**烧录阶段**：先通过 USB 给开发板烧录程序，**烧录完成后请断开 USB（拔掉电脑连接）**。

**上电接线**：**断电状态下完成所有接线**，确认无短路/反接后，再给电源模块上电。

**使用阶段**：**设备运行时**不要让开发板连接在电脑 USB 上（避免外部电源与电脑 USB 供电冲突，或异常电流回灌，导致电脑 USB 口受损，或者把开发版烧了）。

作者烧了块开发板，很难闻。

---

## 更新日志

v1.2.0

使用doom_fire算法实现火苗模拟，详见 `examples/esp32/README.md`

将灯板固定参数如宽高蛇形走位映射，整合入panel_config.h

模拟算法加入esp_dsps加速功能减少性能开销，另外提供without_dsps版本

将重力相关文件整合，减少对外暴露接口

将模拟管理模块成熟化，现在通过sim_manager实现模拟器管理（现在默认通过GPIO_0连接的按键切换。长按——切换模拟器；短按——切换颜色）

v1.0.1

在water_sim.c加入SERPENTINE参数，默认为1，即支持蛇形走位映射。如果使用非蛇形走位的其他灯板，可将SERPENTINE改为0。

v1.0.2

加入test.c测试文件检验灯板好坏，替换main.c即可使用

小tips：用加热台比较好焊接，灯板全亮代码（十分之一亮度），测试一下是否虚焊，ws2812灯珠是串行的，虚焊一般发生在亮与不亮的交界处，重新加热替换即可

---

## Highlight

- **算法驱动的实时效果**：效果在主控端实时计算并渲染到矩阵像素
- **可扩展效果框架**：方便新增/切换不同效果
- **传感器交互**：可接入 MPU6050 等，让画面随倾斜/晃动产生响应
- **适配 WS2812 矩阵**：支持蛇形走位映射与像素坐标转换

---

## 仓库结构

- `fluid2d/`：平台无关 C++11 FLIP/PIC 流体核心目标位置
- `doom_fire/`：独立的 DOOM Fire 算法目标位置
- `examples/esp32/`：当前可编译的 ESP-IDF 参考应用和原始算法实现
- `examples/hardware/`：当前 ESP32 灯板参考硬件
- `prebuilt/esp32/`：面向复刻用户的 ESP32 预编译简易固件
- `docs/`：架构方案、购置清单和图片资源

---
## License

- Firmware/Code: MIT License (see `examples/esp32/LICENSE`)
- Hardware: CC BY-NC 3.0 (see `examples/hardware/LICENSE`)
- Docs: CC BY-NC 3.0 (see `docs/LICENSE`)

Note: Third-party libraries/components remain under their respective licenses.


---
## Attribution

Portions of the firmware are adapted/ported from work by **Matthias Müller (Ten Minute Physics)**,
licensed under the MIT License.

- Original source: https://github.com/matthias-research/pages
- See `examples/esp32/LICENSE` for the full license text.

## 致谢

https://www.youtube.com/watch?v=XmzBREkK8kY
