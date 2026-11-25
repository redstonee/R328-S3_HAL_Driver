# SPI、I²C / TWI、I²S / IIS

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

```c
HAL_StatusTypeDef status = Board_SPI1_Init(1000000U);
/* Check status before using the handle. */
```

I²C 和 I²S 对应封装为 `Board_I2C1_Init(100000U)`、`Board_I2S0_Init(48000U, I2S_DATAFORMAT_16B)`。使用 UART0 的 PB8/PB9 备用路由时，`Board_I2C1_Init()` 返回 `HAL_ERROR`，避免引脚冲突。

其他控制器实例需自行补充 MSP；GPIO 的 Alternate 数值必须匹配实例与封装引脚。GPIO 驱动强度由 `Drive` 的 0–3 档指定，不使用 STM32 的 Speed 语义。

## SPI

支持 SPI0/1 主机、8 位字长、模式 0–3、MSB/LSB first，以及轮询发送、接收和全双工。模块时钟使用 OSC24M，最高 SCLK 为 12 MHz；从两种分频器选择不超过请求值的最快速率，实际值在 `ActualBaudRateHz`。纯接收时 MOSI 发送 `0xFF`。

每批最多 64 字节，预填 TX FIFO 后启动交换。长调用在批次之间可能有时钟间隙；控制器片选在整次调用内保持有效。需要连续无间隙时钟的设备应使用后续 DMA 实现。

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

一次完整设备事务包含选中、命令、数据和释放，须由应用串行执行。句柄锁不覆盖多次 HAL 调用，不能从 IRQ 插入另一设备事务。更改模式或速率前释放全部片选，再修改 `Init` 并重新初始化。

`SPI_NSS_HARD_OUTPUT` 使用 `Init.ChipSelect=0..3`，由控制器管理选定 SS；对应复用引脚需在 MSP 中配置。

FIFO 错误或超时通过 CCU 复位终止交换，句柄回到 RESET。重新调用 `HAL_SPI_Init()` 后才可重试；GPIO 片选仍由应用释放。

## I²C / TWI

支持 TWI0/1 的传统 TWI engine、7 位主机、最高 400 kHz。`I2C0/1` 与 `TWI0/1` 是实例别名。速率由 APB2 按 `APB2 / (10 × 2^N × (M+1))` 分频，实际值记录在 `ActualClockSpeed`。

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

当前使用手册的 SDM 配方和 `PLL_AUDIO(4X)`，按寄存器公式记录为 294.912 / 270.9504 MHz。主机必须能精确分频产生 BCLK 和所需 MCLK，否则初始化失败。默认 32 位时隙封装接受 24/48/96/192 kHz 或 22.05/44.1/88.2/176.4 kHz；当前时钟配置不支持 8/16/32 kHz。手册部分示例与公式不一致，实际输出时钟仍需测量确认。

每次调用处理有限音频块：预填、启动、轮询、等待尾帧、停止。最后一帧后保留至少约 1 ms 尾部时钟，可能输出零样本；调用间存在间隙。连续播放/录音需要 DMA 或中断环形缓冲。

## 例程与验证

见 [独立例程](../example/README.md) 和 [ILI9341 + XPT2046](../example/spi/ili9341/README.md)。默认应用不主动初始化 SPI、I²C 或 I²S。

主机模型覆盖 SPI 跨 FIFO 收发、片选保持、I²C repeated START / NACK / 仲裁丢失、I²S FIFO / 声道映射、PLL 共享和错误恢复。显示模型另检查像素格式、裁剪、触摸解码与事务切换。模型不模拟真实时钟、电气和信号时序。

寄存器依据：R328-S3 User Manual V1.4 的 CCU、I²S/PCM、TWI、SPI 和 GPIO 章节。

GPIO 输入、输出和 EINT 中断接口见 [GPIO 例程说明](../example/gpio/README.md)。更多总线场景包含 SPI 环回、I²C 扫描 / 寄存器读取和 I²S 波形输出，见 [例程索引](../example/README.md)。
