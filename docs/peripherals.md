# SPI、I²C / TWI、I²S / IIS、LEDC

接口采用 `Handle / Init / MspInit / HAL_StatusTypeDef`。先调用 `HAL_Init()` 建立时基，再配置 APB2 和控制台，之后按需初始化外设。每个控制器使用一个持续有效的句柄，仅供 CPU0 使用；轮询调用建议放在前台。

## 初始化与引脚

HAL 负责控制器寄存器，MSP 负责引脚复用。`BSP/` 提供例程使用的初始化封装，默认配置如下；实际接线变化时应同步修改 MSP 和设备控制引脚。

| 接口 | 默认引脚 | 配置位置 |
| --- | --- | --- |
| UART0 TX / RX | PH0 / PH1，AF3 | `Core/Src/r328_hal_msp.c` |
| SPI1 SCLK / MOSI / MISO | PH5 / PH6 / PH7，AF3 | `Core/Src/r328_hal_msp.c` |
| SPI1 两路 GPIO CS | PH4 / PH8，低有效 | MSP 和 `BSP/Src/board_bus.c` |
| TWI1 SCL / SDA | PB8 / PB9，AF4 | `Core/Src/r328_hal_msp.c` |
| I²S0 LRCK / BCLK / DOUT0 | PB2 / PB3，AF5；PB4，AF4 | `Core/Src/r328_hal_msp.c` |
| LEDC DO | PE2，AF2 | `Core/Src/r328_hal_msp.c` |

```c
HAL_StatusTypeDef status = Board_SPI1_Init(1000000U);
/* Check status before using the handle. */
```

I²C 和 I²S 对应封装为 `Board_I2C1_Init(100000U)`、`Board_I2S0_Init(48000U, I2S_DATAFORMAT_16B)`。使用 UART0 的 PB8/PB9 备用路由时，`Board_I2C1_Init()` 返回 `HAL_ERROR`，避免引脚冲突。

其他控制器实例需自行补充 MSP；GPIO 的 Alternate 数值必须匹配实例与封装引脚。GPIO 驱动强度由 `Drive` 的 0–3 档指定，不使用 STM32 的 Speed 语义。

## SPI

支持 SPI0/1 主机、8 位字长、模式 0–3、MSB/LSB first，以及轮询 / IRQ / DMA 发送、接收和全双工。最高 SCLK 为 100 MHz。驱动在 OSC24M 与 `PLL_PERI0(1X) / 3 = 200 MHz` 两个模块时钟及两种 CCR 分频器中选择不超过请求值的最快速率，相同时优先 OSC24M；实际值在 `ActualBaudRateHz`。例如 2 MHz 使用 OSC24M / 12，10 MHz 使用 200 MHz / 20，100 MHz 使用 200 MHz / 2。纯接收时 MOSI 发送 `0xFF`。

需要 PLL 时，驱动启用并等待固定 600 MHz 的 PLL_PERI0(1X) 锁定，已开启则复用，不重调其他总线与存储共用的 PLL。已开启 PLL 的频率或输出配置不兼容时返回 `HAL_ERROR`，锁定超时返回 `HAL_TIMEOUT`。采样模式随实际 SCLK 配置：不超过 24 MHz 使用正常采样，24–60 MHz 使用半周期延迟，达到 60 MHz 使用一周期延迟。高速外设、接线与 GPIO 驱动强度需分别验证。

轮询和中断模式每批最多 64 字节，预填 TX FIFO 后启动交换。长调用在批次之间可能有时钟间隙；控制器片选在整次调用内保持有效。DMA 使用一次交换，由硬件持续补充和排空 FIFO；RX 满时控制器可暂停时钟，不保证绝对无间隙。

例程通过 `SPI_NSS_SOFT` 和 GPIO 管理共享总线片选：

```c
uint8_t command = 0x2CU;
if (Board_SPI1_Select(BOARD_SPI_LCD) == HAL_OK) {
    /* Set D/C, then send the command and its data. */
    HAL_StatusTypeDef status = HAL_SPI_Transmit(&hspi1, &command, 1U, 100U);
    (void)Board_SPI1_Select(BOARD_SPI_NONE);
    (void)status;
}
```

一次完整设备事务包含选中、命令、数据和释放，须由应用串行执行。句柄锁不覆盖多次 HAL 调用，不能从 IRQ 插入另一设备事务。更改模式前释放全部片选，再修改 `Init` 并重新初始化。仅更改速率可在 READY 且全部片选释放时调用 `HAL_SPI_SetBaudRate(&hspi1, hz)`；该接口保留 DMA 连接与异步超时配置，BUSY 时拒绝调速。PLL 配置失败时保持原速率，不能继续选中目标设备传输。

`SPI_NSS_HARD_OUTPUT` 使用 `Init.ChipSelect=0..3`，由控制器管理选定 SS；对应复用引脚需在 MSP 中配置。

FIFO 错误或超时通过 CCU 复位终止交换，句柄回到 RESET。重新调用 `HAL_SPI_Init()` 后才可重试；GPIO 片选仍由应用释放。

### 中断与 DMA

`HAL_SPI_Transmit/Receive/TransmitReceive` 均有 `_IT` / `_DMA` 版本，无 timeout 参数，启动成功返回 `HAL_OK`，完成后返回 READY。三个方向均支持；纯接收由硬件发送 `0xFF`，不需要额外 TX DMA。异步长度必须大于零；DMA 单次长度不超过 `0xFFFFFF` 字节（24 位交换计数）。全双工 DMA 的 TX/RX 缓冲区必须分离，禁止重叠。

`_IT` 自动注册 SPI GIC 中断，在 TC 中断中收取当前批次并预填下一批，保留片选。`_DMA` 使用初始化并连接到 `hdmatx/hdmarx` 的 DMA 句柄，SPI0/1 请求号为 22/23；全双工需要两个不同通道。RX 通道先启动，TX 在启动交换前短暂等待 DMA 预填，避免空 FIFO 起传。DMA 通道采用请求握手，SPI 的 NDMA_MODE 配置为控制器驱动握手信号。

```c
static DMA_HandleTypeDef tx = {.Init = {0U, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_SPI1}};
static DMA_HandleTypeDef rx = {.Init = {1U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_SPI1}};
static uint8_t send[131], receive[131];
HAL_DMA_Init(&tx);
HAL_DMA_Init(&rx);
hspi1.hdmatx = &tx;
hspi1.hdmarx = &rx;
HAL_SPI_TransmitReceive_DMA(&hspi1, send, receive, sizeof(send));
/* Call HAL_SPI_Process(&hspi1) regularly in the foreground until callback. */
```

前台定期调用 `HAL_SPI_Process()`，负责最终完成与超时；IRQ / DMA 负责实际数据搬运。`HAL_SPI_TxCpltCallback` / `RxCpltCallback` / `TxRxCpltCallback` / `ErrorCallback` 均在 Process 或 Abort 的前台上下文执行，回调前已解锁。SPI TC、交换停止以及所有所需 DMA 通道完成后才释放缓冲区；DMA 完成后还检查 FIFO 无残留。所有传输 API 仅从 CPU0 前台调用，不在其他 IRQ 中交叉操作。

默认异步超时为 100 ms，READY 时可调用 `HAL_SPI_SetAsyncTimeout()` 修改；`HAL_MAX_DELAY` 禁用期限，零值无效。长事务应按速率增加期限。`HAL_SPI_Abort()` 通过复位控制器并停止 DMA 中止传输，以 `HAL_SPI_ERROR_ABORT`（8）报告并使句柄 RESET，需重新 Init。FIFO 错误（1）、超时（2）、DMA 错误（4）同样复位；GPIO CS 由应用释放。

已启动的 DMA 若中止超时，SPI 保持 BUSY 与缓冲区所有权，后续 Process 重试清理。通常启动失败不触发回调；如果部分通道已启动且无法停止，启动返回 `HAL_TIMEOUT`，保留 BUSY，最终清理后触发错误回调。句柄 BUSY 时不能释放或修改缓冲区、DMA 句柄和配置。DMA 缓冲区必须完整位于 SRAM `0x20000..0x3FFFF` 或 DDR `0x40000000..0x47FFFFFF`，MMU / D-cache 保持关闭；调用栈上的缓冲区也须等最终回调后才离开作用域。

[ILI9341 + XPT2046 例程](../example/spi/ili9341_xpt2046/README.md) 使用同一组设备测试三种模式，显示辅助层会等待异步完成后才更改 D/C 或片选。

## I²C / TWI

支持 TWI0/1 的轮询、中断和 DMA，7 位主机、最高 400 kHz。轮询 / IRQ 使用传统 TWI engine，DMA 使用独立 packet engine 的 32 字节 FIFO。`I2C0/1` 与 `TWI0/1` 是实例别名。速率由 APB2 按 `APB2 / (10 × 2^N × (M+1))` 分频，实际值记录在 `ActualClockSpeed`。

地址遵循 STM32 HAL 约定：**7 位地址左移一位**，读写位由驱动设置。例如地址 `0x50` 传入 `0x50U << 1`。

```c
uint8_t bytes[4];
HAL_StatusTypeDef status = HAL_I2C_Mem_Read(
    &hi2c1, 0x50U << 1, 0x0123U, I2C_MEMADD_SIZE_16BIT,
    bytes, sizeof(bytes), 100U);
```

`Master_Transmit/Receive` 用于无寄存器地址的设备；`Mem_Write/Read` 支持 8/16 位寄存器地址，16 位地址高字节先发。寄存器读取使用 repeated START，接收末字节发送 NACK。`HAL_I2C_IsDeviceReady()` 进行地址探测，按 trials 重试地址 NACK。

TWI 复用功能自身控制开漏，`GPIO_MODE_AF_PP` 在此仅表示选择复用。MSP 的内部弱上拉不能替代按速率与负载设计的外部上拉。

`ErrorCode` 区分 NACK、仲裁丢失、总线错误和超时。NACK 后发送 STOP；仲裁丢失时不发送 STOP。总线错误或超时后复位控制器，需重新初始化；驱动不提供 GPIO 九脉冲总线解锁。

### 中断与 DMA

`Master_Transmit/Receive`、`Mem_Write/Read` 均有 `_IT` / `_DMA` 版本，无 timeout 参数，启动成功立即返回 `HAL_OK`，完成后句柄回到 READY。启动失败直接返回错误，不触发回调；正在传输时其他调用返回 `HAL_BUSY`。所有传输 API 从前台调用，不要在其他 IRQ 中交叉使用同一句柄。

`_IT` 自动注册 TWI GIC 中断，逐字节推进 START、地址、寄存器、repeated START 和末字节 NACK。`_DMA` 自动注册 TWI 中断，并使用已初始化且连接到 `hdmatx/hdmarx` 的 DMA 句柄；TWI0/1 的 DMA 请求号为 45/46。寄存器前缀最多 2 字节由 CPU 预填，载荷由系统 DMA 搬运。DMA 单次载荷范围 1–65535 字节，不支持零长度探测；设备探测继续使用 `HAL_I2C_IsDeviceReady()`。

例如初始化两个静态 DMA 句柄后连接 I²C：

```c
static DMA_HandleTypeDef tx = {.Init = {0U, DMA_MEMORY_TO_PERIPH, DMA_REQUEST_TWI1}};
static DMA_HandleTypeDef rx = {.Init = {1U, DMA_PERIPH_TO_MEMORY, DMA_REQUEST_TWI1}};
/* Check each initialization result before starting a transfer. */
HAL_DMA_Init(&tx);
HAL_DMA_Init(&rx);
hi2c1.hdmatx = &tx;
hi2c1.hdmarx = &rx;
static uint8_t sample[6];
HAL_I2C_Mem_Read_DMA(&hi2c1, 0x76U << 1, 0xF7U, 1U, sample, sizeof(sample));
/* Foreground loop: */
HAL_I2C_Process(&hi2c1);
```

必须在前台循环定期调用 `HAL_I2C_Process()`，处理 STOP 完成和期限；时基没有周期中断，不能只启动后永久 WFI。默认异步超时为 100 ms，READY 时可用 `HAL_I2C_SetAsyncTimeout()` 修改，`HAL_MAX_DELAY` 取消期限。Process 不代替 IRQ，不搬运字节；TWI 总线完成与 DMA 队列完成都到达后才报告成功，避免 TX 数据刚进 FIFO 就释放应用缓冲区。

回调为 `HAL_I2C_MasterTxCpltCallback` / `MasterRxCpltCallback` / `MemTxCpltCallback` / `MemRxCpltCallback` 和 `HAL_I2C_ErrorCallback`。I²C 回调均在 Process 或显式 Abort 的调用上下文执行，回调前已解锁。`HAL_I2C_Abort()` 终止异步传输，以 `HAL_I2C_ERROR_ABORT` 报告并使句柄 RESET，重新 Init 后重试；DMA 模式下的总线错误（含 NACK）也复位控制器。

DMA 底层支持 10 通道、8 位宽度、单次非循环传输；共享 DMA IRQ 自动分发，不复位其他通道。已占用通道拒绝第二个句柄。所有句柄和缓冲区从启动到最终回调必须持续有效；传输期间不要修改缓冲区、DMA 配置或复用对应通道。DMA 只接受 SRAM `0x20000..0x3FFFF` 或 128 MiB DDR 窗口中的完整数据缓冲区，使用物理地址，要求维持当前启动代码关闭 MMU / D-cache 的条件；自行开启缓存时须先增加缓存维护。

DMA 中止会等待通道停止；极端硬件故障下中止超时，句柄保持 BUSY，不提前释放缓冲区，后续 Process 会继续尝试清理。新增 `HAL_I2C_ERROR_DMA`（16）和 `HAL_I2C_ERROR_ABORT`（32）用于诊断。BMP280 的接线、三种模式切换和复制步骤见 [BMP280 例程](../example/i2c/bmp280/README.md)。

DMA 通道使用源/目的握手模式（MODE bits 2/3），等待外设刷新 FIFO 请求后再传下一个字节；固定等待周期不能保证请求已经撤销，可能读空 FIFO。该设置也见 [全志 DMA 驱动](https://github.com/Allwinner-Homlet/H6-BSP4.9-linux/blob/master/drivers/dma/sunxi-dma.c#L521)。I²C DMA 接收在总线与 DMA 均完成后还检查 RX FIFO 必须为空，残留数据按 DMA 错误处理。

## I²S / IIS

支持 I2S0/1/2、立体声双时隙、Philips I²S 或左对齐、16/24/32 位采样和时隙、主从模式及单向或全双工轮询。例程封装固定为主机发送、32 位时隙、无 MCLK；输入和 MCLK 需补充 MSP。

```c
static const uint32_t silence[256] = {0}; /* 128 stereo frames. */
if (Board_I2S0_Init(48000U, 16U) == HAL_OK) {
    HAL_StatusTypeDef status = HAL_I2S_Transmit(&hi2s0, silence, 256U, 100U);
    (void)status;
}
```

缓冲区统一为 `uint32_t`，有效采样放在低位，顺序为 `L0,R0,L1,R1,...`；接收使用硬件符号扩展。size 为声道采样数，必须为偶数，不是字节数或帧数。

音频 PLL 为共享资源。初始化前显式调用 `HAL_RCC_AudioPLL_Config()`；BSP 封装已代为调用。更换频率族前须释放音频用户；其他 I²S、DMIC、Codec 或 OWA 时钟开启时，重调 PLL 返回 `HAL_BUSY`。

当前使用手册的两组 SDM 配方，送到 R328 I²S 的源时钟按 **24.576 / 22.5792 MHz** 计算。CCU 将该来源标为 `PLL_AUDIO(4X)`，`HAL_RCC_GetAudioPLL4XFreq()` 沿用此命名；不能按未经过后分频的公式当成 294.912 / 270.9504 MHz。频率查询同时检查配方中的 P=11。主机必须能精确分频产生 BCLK 和所需 MCLK，否则初始化失败。

默认 32 位时隙封装接受 8/12/16/24/32/48/96/192 kHz 或 11.025/22.05/44.1/88.2/176.4 kHz。48 kHz 立体声使用 24.576 MHz / 8，得到 3.072 MHz BCLK，再按每帧 64 位得到 48 kHz LRCK；关闭 MCLK 时 `CLKD=0x51`。旧版误用 /96，实际约为 4 kHz，会导致音调偏低且 480 帧在 100 ms 内无法完成。

每次调用处理有限音频块：预填、启动、轮询、等待尾帧、停止。最后一帧后保留至少约 1 ms 尾部时钟，可能输出零样本；调用间存在间隙。连续播放/录音需要 DMA 或中断环形缓冲。

轮询按 FIFO 占用量批量补充 TX / 读取 RX，超时检查在每批之间进行，避免逐样本的时基换算拖慢补数。发送中仍有待提交样本时检测到 TX 缺数即返回 `HAL_ERROR`，`ErrorCode` 包含 `HAL_I2S_ERROR_UDR`（64）；错误后控制器复位，句柄进入 RESET，重新调用 `HAL_I2S_Init()` 才能重试。普通发送错误的处理应保留前台响应，tone 例程会打印诊断并等待下一次 `p` 命令。

## LEDC

支持硬件 LEDC 的轮询整帧发送，1–1024 颗串联 RGB 灯，每灯一个 `uint32_t`，低 24 位为 `0xGGRRBB`，高位先发。`HAL_LEDC_ColorRGB(r, g, b)` 将 RGB 分量转换为传输字。

```c
uint32_t pixels[BOARD_WS2812_LED_COUNT] = {0};
if (Board_LEDC_Init(BOARD_WS2812_LED_COUNT) == HAL_OK)
{
    pixels[0] = HAL_LEDC_ColorRGB(16U, 0U, 0U);
    HAL_StatusTypeDef status = HAL_LEDC_Transmit(
        &hledc, pixels, BOARD_WS2812_LED_COUNT, 100U);
    (void)status;
}
```

LEDC 模块时钟固定为 OSC24M / 1。`Init.T0H/T0L/T1H/T1L` 的单位是 24 MHz 时钟周期，分别限制为 2–32、2–64、2–64、2–32；驱动转换成寄存器的周期数减一编码。`ResetTimeUs` 与 `WaitDataTimeUs` 均为 1–341 µs，后者限制等待 FIFO 后续数据的时间。

`Board_LEDC_Init()` 默认设置 7/23/23/7 周期，即 800 kbit/s、约 292/958 ns 的短/长脉冲、300 µs 帧复位和 50 µs 缺数等待，适配 XL-5050RGBC-WS2812B。该设置与 I²S 音频 PLL、APB2 分频无关。

发送数量必须等于 `Init.LEDCount`。启动前预填最多 32 个 FIFO 字，长帧轮询 FIFO 占用量补数；API 等待整帧完成及最终低电平复位脉冲后返回。`timeout` 使用毫秒，`HAL_MAX_DELAY` 表示不限时。使用期间缓冲区必须有效，且不得被其他上下文修改。当前未实现 DMA 或 LEDC IRQ 发送。

FIFO 溢出、缺数、发送超时和复位失败通过 `ErrorCode` 区分；失败后 CCU 复位中止发送，句柄进入 RESET，须重新调用 `HAL_LEDC_Init()` 后重试。长灯链补数需要足够的 CPU 响应时间；避免在传输期间执行长时间中断处理。默认 4 颗灯可全部预填，不依赖中途补数。

PE2 的 AF2 由 MSP 配置，BSP 不在默认启动流程中开启 LEDC。灯效、复制命令及实板验证记录见 [WS2812 例程](../example/ledc/README.md)。

## 例程与验证

见 [独立例程](../example/README.md)、[ILI9341 + XPT2046](../example/spi/ili9341_xpt2046/README.md) 和 [WS2812](../example/ledc/README.md)。默认应用不主动初始化 SPI、I²C、I²S 或 LEDC。

主机模型覆盖 SPI 跨 FIFO 收发、片选保持、I²C repeated START / NACK / 仲裁丢失、I²S FIFO / 声道映射、PLL 共享和错误恢复。显示模型另检查像素格式、裁剪、触摸解码与事务切换。模型不模拟真实时钟、电气和信号时序。

SPI1 的轮询、中断和 DMA 已通过 FEL SRAM 实板验证：原先 LCD / 触摸均为 2 MHz，以及当前 LCD 10 MHz / 触摸 2 MHz 的 mode 0 配置，三种模式的色条、边框与触摸画点均正常。当前配置的启动串口频率输出也已确认正确。操作见 [触屏例程](../example/spi/ili9341_xpt2046/README.md)。

LEDC 主机模型另覆盖时序字段、32 字 FIFO、最大帧、超时和异常恢复；4 颗 WS2812 已通过 FEL SRAM 实板灯效及串口运行验证。

寄存器依据：R328-S3 User Manual V1.4 的 CCU、I²S/PCM、TWI、SPI、LEDC 和 GPIO 章节。

GPIO 输入、输出和 EINT 中断接口见 [GPIO 例程说明](../example/gpio/README.md)。更多总线场景包含 SPI 环回、I²C 扫描 / 寄存器读取和 I²S 波形输出，见 [例程索引](../example/README.md)。
