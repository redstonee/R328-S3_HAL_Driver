# 外设例程

按外设模块组织，每个子目录提供一个可复制的 `main.c`。应用统一编译 `Core/Src/*.c`，头文件放入 `Core/Inc/`。选择例程时复制源码，CMake 预设只选择内存、加载方式与构建类型。

| 模块 | 例程目录 | 行为 |
| --- | --- | --- |
| [GPIO](gpio/README.md) | [output](gpio/output/main.c) | 输出置位、复位、翻转并回读 |
| GPIO | [input](gpio/input/main.c) | 上拉输入与 20 ms 稳定电平消抖 |
| GPIO | [interrupt](gpio/interrupt/main.c) | 双边沿 EINT、前台消抖、释放中断 |
| [UART](uart/README.md) | [echo](uart/echo/main.c) | 115200 8N1 逐字节回显 |
| UART | [line](uart/line/main.c) | 行缓冲、退格、CR/LF、溢出处理与空闲超时 |
| [Timer](tim/README.md) | [periodic](tim/periodic/main.c) | Timer1 每秒 IRQ 与事件计数 |
| Timer | [oneshot](tim/oneshot/main.c) | 250 ms 单次 IRQ，串口命令重新启动 |
| [时基](timebase/README.md) | [counter](timebase/counter/main.c) | 64 位计数器与关 IRQ 延时计时 |
| 时基 | [timeout](timebase/timeout/main.c) | 250 ms / 1 s 非阻塞任务与超时差值 |
| [SPI](spi/README.md) | [clocks](spi/clocks/main.c) | 保持片选无效，发送测试时钟与数据 |
| SPI | [loopback](spi/loopback/main.c) | 外部 MOSI/MISO 环回，校验跨 FIFO 的 131 字节 |
| SPI | [interrupt](spi/interrupt/main.c) | `_IT` 单向收发、全双工环回、回调与中止 |
| SPI | [dma](spi/dma/main.c) | `_DMA` 单向收发、全双工环回与 DMA 句柄连接 |
| SPI | [ili9341_xpt2046](spi/ili9341_xpt2046/README.md) | ILI9341 色条与 XPT2046 触摸轨迹，轮询 / IRQ / DMA |
| [I²C](i2c/README.md) | [probe](i2c/probe/main.c) | 探测地址 0x50，报告 NACK / 错误码 |
| I²C | [scan](i2c/scan/main.c) | 扫描 0x08–0x77，支持重新扫描 |
| I²C | [memory](i2c/memory/main.c) | 可配置设备与寄存器地址，重复 START 读取 |
| I²C | [interrupt](i2c/interrupt/main.c) | `_IT` 主机收发、寄存器读写、回调与中止 |
| I²C | [dma](i2c/dma/main.c) | `_DMA` 主机收发、寄存器读写与 DMA 句柄连接 |
| I²C | [bmp280](i2c/bmp280/README.md) | 0x76 BMP280，轮询 / IRQ / DMA 温度气压对比 |
| [I²S](i2s/README.md) | [silence](i2s/silence/main.c) | 48 kHz 有限静音块 |
| I²S | [tone](i2s/tone/main.c) | 200 ms PCM16 正弦波、左右声道单独测试 |
| [LEDC](ledc/README.md) | [ws2812](ledc/ws2812/main.c) | 4 颗 WS2812，颜色、流水与呼吸灯效 |
| [SRAM](sram/README.md) | [memory](sram/memory/main.c) | 完整 SRAM 配置下的空闲区写入 / 回读 |
| [DRAM](dram/README.md) | [memory](dram/memory/main.c) | DDR 空闲区 walking 1/0、固定模式与地址 / 反相地址校验 |
| [RCC](rcc/README.md) | [clocks](rcc/clocks/main.c) | 查询 CPU、APB2、架构计数器与音频时钟 |

## 复制与构建

从工程根目录操作。先将自己的 `Core/Src/main.c` 备份到 `Core/Src` 以外的位置；例如运行 GPIO 中断例程：

```sh
cp example/gpio/interrupt/main.c Core/Src/main.c
cp example/gpio/gpio_example.h Core/Inc/
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入 BROM FEL 后：
cmake --build build/fel-debug --target fel-run
```

每个 `main.c` 自行完成 HAL、时钟、UART 和所需外设的初始化与错误处理，无须复制公共代码。只保留一个 `main.c`；不要把备份命名成仍以 `.c` 结尾的文件留在 `Core/Src/`，其中的所有 `.c` 都会参与构建。新增或移除 `.c` 后，构建会自动重新配置。

除了所选例程的 `main.c`，下列例程还需附加文件：

| 例程 | 复制到 `Core/Src/` | 复制到 `Core/Inc/` |
| --- | --- | --- |
| GPIO 的三个例程 | 无 | `gpio/gpio_example.h` |
| I²C memory | 无 | `i2c/memory/config.h` |
| I²C interrupt / dma | 无 | `i2c/i2c_async_config.h` |
| I²C BMP280 | `i2c/bmp280/bmp280.c` | `i2c/bmp280/bmp280.h` |
| SPI 显示 / 触摸 | `spi/ili9341_xpt2046/ili9341.c`、`spi/ili9341_xpt2046/xpt2046.c`、`spi/ili9341_xpt2046/display_bus.c` | `spi/ili9341_xpt2046/ili9341.h`、`spi/ili9341_xpt2046/xpt2046.h`、`spi/ili9341_xpt2046/display_bus.h` |
| 其余例程 | 无 | 无 |

表中的源路径相对于 `example/`。显示 / 触摸的完整复制命令见 [ILI9341 + XPT2046](spi/ili9341_xpt2046/README.md)。切换例程时，移出上一个例程附加的文件。旧工程若曾复制公共 `example.c` / `example.h`，迁移时一并删除，避免重复定义初始化和错误处理。

同一份 `Core` 源码可使用不同内存与加载预设：

| 预设 | 用途 |
| --- | --- |
| `fel-debug` / `fel-release` | BROM FEL 加载，128 KiB SRAM 运行 |
| `fel-direct` | 原 32 KiB SRAM FEL 回退配置 |
| `nand-debug` / `nand-release` | SPI NAND 冷启动，boot0 初始化 DDR 后直接执行应用 |
| `nor-debug` / `nor-release` | W25Q64 SPI NOR 冷启动，初始化 DDR 后直接执行应用 |

NAND 构建使用工程内的必要 prebuilt，命令及页布局见 [NAND 与 DDR 说明](../docs/nand-image.md)。W25Q64 使用相同应用源码，NOR 连接和线性镜像见 [SPI NOR 说明](../docs/nor-image.md)，通过 USB FEL 烧写见 [xfel NOR 烧写](../docs/fel-nor.md)。

xfel 不在 PATH 时，配置阶段增加 `-DR328_XFEL=/absolute/path/to/xfel`。产物位于 `build/<预设名>/`；默认 FEL 暂存于 `0x28000`，通过引导搬到 `0x20000` 执行，运行总容量为 128 KiB，应用 BIN 上限为 64 KiB。普通构建和 CTest 不访问设备，详见 [FEL 说明](../docs/fel-sram.md)。

## 配置与验证

框架不配置全局 console。例程为了交互和观察结果，在本地 `app_init()` 中显式配置 UART0 PH0/PH1、115200 8N1；可自行更换实例、引脚或移除输出。数值格式化通过 `HAL_UART_PrintU32/PrintHex/PrintValue/HexDump` 完成，均须传入句柄和超时。外设示例引脚见 [接口说明](../docs/peripherals.md)。复制后在 `Core/Inc/gpio_example.h` 中修改 GPIO 引脚，在 `Core/Inc/config.h` 中修改 I²C 轮询读取参数，在 `Core/Inc/i2c_async_config.h` 中修改 I²C 中断 / DMA 例程参数，在 `Core/Inc/ili9341.h`、`Core/Inc/xpt2046.h` 中修改显示控制脚与触摸参数；需要调整面板初始化序列时修改 `Core/Src/ili9341.c`。

SPI / I²C 的 `interrupt`、`dma` 为独立通用例程；SPI 显示/触摸和 I²C BMP280 例程支持轮询、中断、DMA 切换。异步完成与超时在前台 Process 中处理，DMA 例程各自占用通道 0 / 1，合并到一个应用时需分配不同通道。GPIO 和 Timer IRQ 回调只记录事件，耗时处理放在前台；仅有自由运行计数器时不能依靠 WFI 唤醒。I²S 为有限块传输，连续播放仍需 DMA 或中断环形缓冲。

主机测试验证寄存器、IRQ 分发与协议逻辑；交叉构建检查镜像入口和内存布局。实际信号、显示效果与触摸校准需在目标硬件上验证。
