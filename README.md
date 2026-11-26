# R328-S3 HAL 裸机框架

面向 Allwinner R328-S3（双核 ARM Cortex-A7）的 C11 + CMake 裸机工程，采用接近 STM32 HAL 的 `Handle / Init / MspInit / Callback` 接口。当前仅使用 CPU0，不依赖操作系统、newlib 或动态内存。

## 快速开始

需要 CMake ≥ 3.21、Ninja、GNU Arm Embedded 工具链（`arm-none-eabi-gcc`、binutils、libgcc）和 Python 3。通过 USB FEL 下载时另需 [xfel](https://github.com/xboot/xfel)。

```sh
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
```

进入 BROM FEL 后执行：

```sh
cmake --build build/fel-debug --target fel-run
```

xfel 不在 PATH 时，在配置命令后增加 `-DR328_XFEL=/absolute/path/to/xfel`。

框架启动不预留 UART 或外设引脚。`Core/Src/main.c` 是应用入口，HAL、时基和时钟初始化之后的外设配置由应用决定。应用自行定义句柄、配置 GPIO 并调用对应 HAL 初始化；需要输出时，显式传入 UART 句柄调用 `HAL_UART_PrintString()`、`HAL_UART_PrintU32()`、`HAL_UART_PrintHex()` 等接口。

`fel-run` 校验芯片 ID，将程序暂存于 `0x28000`，回读比较后通过 scratchpad 引导搬到 `0x20000` 执行，运行时使用完整 128 KiB SRAM；普通构建和 CTest 不访问 USB。详见 [FEL 下载说明](docs/fel-sram.md)。

应用源码统一放在 `Core/Src/`，头文件放在 `Core/Inc/`。运行例程时，将对应的 `main.c` 和设备文件复制到 `Core`，继续使用相同的内存 / 加载预设。每个例程独立完成初始化，按需使用 UART0 PH0/PH1、115200 8N1 输出诊断；框架不设置全局 console。复制步骤见 [例程索引](example/README.md)，显示与触摸见 [ILI9341 + XPT2046](example/spi/ili9341_xpt2046/README.md)。

## 功能

| 模块 | 支持范围 |
| --- | --- |
| 启动 | ARM 异常向量、VBAR、异常模式栈、数据初始化、异常现场记录 |
| GPIO | 输入、推挽输出、复用、上下拉、驱动强度、端口读写、EINT 与中断回调 |
| RCC | APB2、UART/SPI/TWI/I²S/LEDC 时钟与复位、音频 PLL |
| UART | UART0–3 轮询收发、7/8 数据位、校验、超时、字符串 / 数值 / 十六进制输出 |
| SPI | SPI0/1 主机、模式 0–3、8 位轮询 / IRQ / DMA、软件或控制器片选 |
| I²C / TWI | TWI0/1、7 位主机、最高 400 kHz、轮询 / IRQ / DMA、寄存器读写与重复 START |
| DMA | 10 通道、字节宽度单次外设传输、描述符与完成中断；已接入 SPI / I²C |
| I²S / IIS | I2S0/1/2、立体声、16/24/32 位、主从模式、有限块轮询收发 |
| LEDC | 硬件 WS2812 时序、1–1024 灯整帧轮询发送、GRB 格式、超时与错误恢复 |
| TIM / GIC | Timer0/1、周期与单次模式、IRQ 注册、回调与分发 |
| 时基 | 64 位 ARM 架构计数器 CNTPCT，毫秒 tick 和延时，无周期 IRQ |
| 例程 | 按模块细分的 26 个场景，含 SRAM / DRAM 测试、GPIO 中断、SPI / I²C 通用中断与 DMA、显示触摸、BMP280、I²S 波形和 WS2812 灯效 |

SPI NAND / W25Q64 SPI NOR 冷启动共用必要的 32 KiB boot0 prebuilt，初始化 DDR 后复制并直接执行裸机应用；NOR 读取器从源码构建，不依赖完整系统镜像或 U-Boot。其他外设的 DMA 接入、UART 中断收发、PWM、USB 驱动、独立 DDR 训练驱动、SMP 和 MMU/cache 管理尚未实现。HAL 接口按 R328 的硬件能力设计，不保证与 STM32 HAL 源码直接兼容。

## 构建配置

预设按运行内存、加载方式与构建类型划分，所有预设均编译当前 `Core/Src/*.c`。添加或移除该目录中的 `.c` 后，构建会自动重新配置；应用头文件通过 `Core/Inc/` 查找。

| 预设 | 入口 / 内存 | 加载条件 |
| --- | --- | --- |
| `fel-debug` / `fel-release` | `0x20000`，128 KiB | 复位后的 BROM FEL，scratchpad 引导搬运 |
| `fel-direct` | `0x28000`，32 KiB | 原先的保守 FEL 布局，供回退 |
| `nand-debug` / `nand-release` | `0x40000000`，128 MiB | SPI NAND 冷启动，boot0 初始化 DDR 后直接执行应用 |
| `nor-debug` / `nor-release` | `0x40000000`，128 MiB | W25Q64 SPI NOR 冷启动，源码读取器加载应用 |

`sram-debug`、`sram-release` 和 `ddr-debug` 为隐藏基础配置，不出现在预设列表，也不能直接通过 `--preset` 调用。调试器或外部 loader 加载请使用下方的自定义构建方式。

SRAM A1 总容量为 **128 KiB**，默认 FEL 配置全部用于代码、静态数据、空闲区和栈。应用 BIN 暂存上限为 **64 KiB**，BSS 不占用 BIN。可复制 [SRAM 例程](example/sram/README.md) 运行空闲区写入 / 回读测试；加载流程和限制见 [内存说明](docs/fel-sram.md#容量和限制)。

DDR 硬件容量按 **128 MiB** 配置。NAND / NOR 启动不加载常驻安全固件，应用使用完整 DDR 链接窗口。可复制 [DRAM 测试例程](example/dram/README.md) 检查代码、静态数据和栈以外的空闲 DDR。构建 NAND 启动镜像：

```sh
cmake --preset nand-debug
cmake --build --preset nand-debug
ctest --preset nand-debug
```

产物为 `build/nand-debug/firmware-nand.bin`，包含 boot0 和应用包的多个副本。它是物理页主数据镜像，烧写时须启用芯片内部 ECC、保留坏块标记及物理偏移。默认芯片为 **MX35LF1GE4AB（128 MiB，ID C2 12）**。进入 BROM FEL 后，使用 `cmake --build build/nand-debug --target fel-probe` 检测并扫描启动区坏块，`--target fel-flash` 烧写并回读校验，`--target fel-verify` 单独校验。只擦写前 4 MiB 中的好块，保留物理偏移及其余 124 MiB。步骤见 [xfel NAND 烧写](docs/fel-nand.md)，格式见 [SPI NAND 与 DDR 说明](docs/nand-image.md)。

MX35LF1GE4AB 已通过全量回读及断电冷启动验证，应用 tick 正常。boot0 必须使用每页完整 2 KiB 的布局，副本位于块 0、2、4、6；旧版每页 1 KiB 的镜像需重新打包烧写，见 [ROM 排查与验证记录](docs/nand-boot-debug.md)。

W25Q64 使用 `cmake --preset nor-debug`、`cmake --build --preset nor-debug`，生成 **8 MiB** 的 `build/nor-debug/firmware-nor.bin`，从 Flash 地址 0 写入；应用包位于 `0x10000`。进入 BROM FEL 后可用 `cmake --build build/nor-debug --target fel-probe` 检测、`--target fel-flash` 整片擦写并回读校验，无需修改原版 xfel；**烧写会覆盖整颗 8 MiB Flash**。使用 SPI0 的 PC0 / PC2 / PC3 / PC4，boot0 日志默认 PH0 / PH1，应用自行配置 UART。接线和布局见 [SPI NOR 说明](docs/nor-image.md)，USB 烧写步骤见 [xfel NOR 烧写](docs/fel-nor.md)。擦写和冷启动均尚未实板验证。

每个构建目录生成 `firmware.elf`、`firmware.bin`、`firmware.hex`、`firmware.map` 和 `compile_commands.json`。FEL 配置另生成 `firmware-fel.bin` 与描述地址、长度、SHA256 的 `firmware-fel.json`；FULL 配置还生成配套的 `firmware-fel-loader.bin`。

自定义构建：

```sh
cmake -S . -B build/custom -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake \
  -DCMAKE_BUILD_TYPE=Debug -DR328_MEMORY=SRAM
cmake --build build/custom
```

常用选项：

| 选项 | 用途 |
| --- | --- |
| `ARM_TOOLCHAIN_PREFIX` | 工具链前缀，默认 `arm-none-eabi`，可指定完整路径前缀 |
| `R328_BOOT_UART0_ROUTE` | 仅 NAND / NOR boot0 日志路由：`PH0_PH1`（默认）或 `PB8_PB9`；不配置应用 UART |
| `R328_GIC_SECURITY` | `SECURE`（默认）或 `NONSECURE`，须匹配实际运行状态 |
| `R328_DDR_ORIGIN` / `R328_DDR_LENGTH` | DDR 应用地址和范围；默认 128 MiB，NAND / NOR 配置固定使用此窗口 |
| `R328_NAND_IMAGE` | 启用 SPI NAND 直接启动头部和镜像打包 |
| `R328_NAND_PROFILE` | NAND 配置路径，默认 `prebuilt/r328/spinand.json`（MX35LF1GE4AB） |
| `R328_NOR_IMAGE` | 启用 W25Q64 NOR 读取器和镜像打包，与 NAND / FEL 互斥 |
| `R328_FEL_SRAM_PROFILE` | `FULL`（默认，128 KiB，搬运）或 `DIRECT`（32 KiB，直接执行） |
| `R328_XFEL` | xfel 可执行文件路径 |
| `BUILD_TESTING` | 是否启用构建产物检查，默认开启 |

普通 SRAM / DDR 的 BIN 需要按链接地址加载，不是独立的 Flash 冷启动镜像。

## 目录与移植

```text
Core/                       默认应用、MSP、异常入口、启动汇编
Drivers/Device/Include/     寄存器、IRQ ID、Cortex-A7 原语
Drivers/R328_HAL_Driver/    HAL 公共接口和实现
prebuilt/r328/             必要的 32 KiB boot0 二进制与 NAND / NOR 配置
example/                   各外设独立例程及显示、触摸测试
boot/nor/                  从源码构建的 SPI NOR 启动读取器
cmake/                     CMake的GNU Arm 工具链配置文件
linker/                    RAM 链接脚本模板
tests/                     主机模型测试和 ARM 产物检查
tools/                     FEL / NAND / NOR 打包、校验、下载和 Flash 烧写脚本
docs/                      加载、时基、外设和开发说明
```

初始化流程为 `Reset_Handler → SystemInit → main → HAL_Init → SystemClock_Config`。`HAL_Init()` 初始化 GIC 和计数器时基后开放 CPU IRQ；`SystemInit()` 保留加载方设置的 PLL、DDR、LDO 和 SRAM 状态。

外设句柄、引脚和初始化顺序由应用管理。可直接在 `main.c` 中配置，也可在 `Core/Src/r328_hal_msp.c` 中覆盖 HAL 的弱 MSP 钩子。例程的配置都在各自目录内，不依赖 BSP 或公共例程库；迁移时同步修改外设实例、复用、片选和控制脚。

HAL 仅支持 CPU0，句柄锁通过 CPU0 IRQ 临界区保护，不提供跨核同步。句柄应清零初始化并在使用期间保持有效。轮询 API 使用毫秒超时；阻塞操作建议放在前台。`HAL_GetTick()` 约每 49.7 天回绕，超时比较使用无符号差值。

## 文档与验证

- [FEL 下载与 SRAM 布局](docs/fel-sram.md)
- [SPI NAND 烧录包与 DDR 初始化](docs/nand-image.md)
- [W25Q64 SPI NOR 镜像与接线](docs/nor-image.md)
- [通过 xfel 烧写 W25Q64](docs/fel-nor.md)
- [通过 xfel 烧写 MX35LF1GE4AB](docs/fel-nand.md)
- [ARM 架构计数器时基](docs/timebase.md)
- [SPI、I²C、I²S 和 LEDC 接口](docs/peripherals.md)
- [WS2812 灯效例程](example/ledc/README.md)
- [独立例程](example/README.md)
- [代码格式与开发检查](docs/development.md)

主机测试使用独立构建目录：

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

测试覆盖 UART 输出格式 / 锁 / 超时、寄存器操作、GPIO EINT/GIC 分发、总线状态机、LEDC FIFO 与错误恢复、超时回绕、时基、显示/触摸协议、FEL 下载流程和 NOR 启动读取协议；交叉构建另检查入口、向量、RAM 边界、栈及未解析符号。模型测试不替代真实信号和设备验证。

寄存器依据为 R328-S3 User Manual V1.4 和 Datasheet。原接口参考 [R328-S3_StdPeriphDriver](https://github.com/redstonee/R328-S3_StdPeriphDriver)，Cortex-A 异常与中断参考 [Arm 编程指南](https://documentation-service.arm.com/static/6893ad23e7f7ce6150e88d37) 和 [GIC 规范](https://documentation-service.arm.com/static/5f8ff196f86e16515cdbf969)。构建不在线下载这些资料或代码。
