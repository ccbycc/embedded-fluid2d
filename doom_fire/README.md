# DOOM Fire

DOOM Fire 是独立效果模块，不属于 FLIP/PIC 核心。

后续迁移到这里的实现应只维护火焰状态并输出热度网格。调色板、gamma、LED 亮度限制、传感器读取和任务调度继续由应用层负责。当前可运行实现仍位于 `examples/esp32/components/Middlewares/DOOM/`。
