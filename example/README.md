# 外设例程

按外设模块组织，每个子目录包含独立 `main.c`，一次只链接一个例程。`common/` 提供初始化、状态输出、十六进制转储和错误处理；默认应用为 `Core/Src/main.c`。

| 模块 | 例程目录 | FEL 预设 | 行为 |
| --- | --- | --- | --- |
| [GPIO](gpio/README.md) | [output](gpio/output/main.c) | `fel-gpio-output` | 输出置位、复位、翻转并回读 |
| GPIO | [input](gpio/input/main.c) | `fel-gpio-input` | 上拉输入与 20 ms 稳定电平消抖 |
| GPIO | [interrupt](gpio/interrupt/main.c) | `fel-gpio-interrupt` | 双边沿 EINT、前台消抖、释放中断 |
| [UART](uart/README.md) | [echo](uart/echo/main.c) | `fel-uart-echo` | 115200 8N1 逐字节回显 |
| UART | [line](uart/line/main.c) | `fel-uart-line` | 行缓冲、退格、CR/LF、溢出处理与空闲超时 |
| [Timer](tim/README.md) | [periodic](tim/periodic/main.c) | `fel-tim-periodic` | Timer1 每秒 IRQ 与事件计数 |
| Timer | [oneshot](tim/oneshot/main.c) | `fel-tim-oneshot` | 250 ms 单次 IRQ，串口命令重新启动 |
| [时基](timebase/README.md) | [counter](timebase/counter/main.c) | `fel-timebase-counter` | 64 位计数器与关 IRQ 延时计时 |
| 时基 | [timeout](timebase/timeout/main.c) | `fel-timebase-timeout` | 250 ms / 1 s 非阻塞任务与超时差值 |
| [SPI](spi/README.md) | [clocks](spi/clocks/main.c) | `fel-spi-clocks` | 保持片选无效，发送测试时钟与数据 |
| SPI | [loopback](spi/loopback/main.c) | `fel-spi-loopback` | 外部 MOSI/MISO 环回，校验跨 FIFO 的 131 字节 |
| SPI | [ili9341](spi/ili9341/README.md) | `fel-spi-ili9341` | ILI9341 色条与 XPT2046 触摸轨迹，共享 SPI |
| [I²C](i2c/README.md) | [probe](i2c/probe/main.c) | `fel-i2c-probe` | 探测地址 0x50，报告 NACK / 错误码 |
| I²C | [scan](i2c/scan/main.c) | `fel-i2c-scan` | 扫描 0x08–0x77，支持重新扫描 |
| I²C | [memory](i2c/memory/main.c) | `fel-i2c-memory` | 可配置设备与寄存器地址，重复 START 读取 |
| [I²S](i2s/README.md) | [silence](i2s/silence/main.c) | `fel-i2s-silence` | 48 kHz 有限静音块 |
| I²S | [tone](i2s/tone/main.c) | `fel-i2s-tone` | PCM16 左 1 kHz / 右 500 Hz 三角波数据块 |
| [SRAM](sram/README.md) | [memory](sram/memory/main.c) | `fel-sram-memory` | 完整 SRAM 配置下的空闲区写入 / 回读 |
| [RCC](rcc/README.md) | [clocks](rcc/clocks/main.c) | `fel-rcc-clocks` | 查询 CPU、APB2、架构计数器与音频时钟 |

## 构建与运行

例如 GPIO 中断：

```sh
cmake --preset fel-gpio-interrupt
cmake --build --preset fel-gpio-interrupt
ctest --preset fel-gpio-interrupt
# 进入 BROM FEL 后：
cmake --build build/fel-gpio-interrupt --target fel-run
```

运行显示和触摸时，将预设替换为 `fel-spi-ili9341`。xfel 不在 PATH 时，配置阶段增加 `-DR328_XFEL=/absolute/path/to/xfel`。

产物位于 `build/<预设名>/`，默认 FEL 暂存于 `0x28000`，通过引导搬到 `0x20000` 后执行，运行总容量为 128 KiB，应用 BIN 上限为 64 KiB。普通构建和 CTest 不访问设备，详见 [FEL 说明](../docs/fel-sram.md)。

普通 SRAM / 已初始化 DDR 也可通过模块路径选择例程：

```sh
cmake --preset ddr-debug -DR328_EXAMPLE=spi/ili9341
cmake --build --preset ddr-debug
```

该 BIN 需按 DDR 链接地址由外部 loader 加载。`R328_EXAMPLE` 为空时使用 `R328_APP_SOURCE`，不会改写默认应用。

## 配置与兼容

外设默认引脚见 [接口说明](../docs/peripherals.md)。GPIO 示例引脚集中在 `gpio/gpio_example.h`；I²C 寄存器读取参数在 `i2c/memory/config.h`；显示控制脚和触摸校准参数在 `spi/ili9341/`。按实际接线调整配置。

原预设仍可使用：`fel-gpio`、`fel-uart`、`fel-tim`、`fel-timebase`、`fel-spi`、`fel-i2c`、`fel-i2s`、`fel-ili9341`，分别指向原有基础功能。原来不带子目录的 `R328_EXAMPLE` 值也保留为别名，新配置推荐使用完整模块路径。

例程共用轮询式 HAL。GPIO 和 Timer IRQ 回调只记录事件，耗时处理放在前台；仅有自由运行计数器时不能依靠 WFI 唤醒。I²S 为有限块传输，连续播放仍需 DMA 或中断环形缓冲。

主机测试验证寄存器、IRQ 分发与协议逻辑；各交叉构建检查镜像入口和内存布局。实际信号、显示效果与触摸校准需在目标硬件上验证。
