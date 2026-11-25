# I²S / IIS

| 例程 | 内容 |
| --- | --- |
| [silence](silence/main.c) | 一次发送 128 帧静音，然后串口回显 |
| [tone](tone/main.c) | 连续 200 ms PCM16 正弦波，支持立体声与左右声道单独测试 |

默认 I²S0 主机发送，48 kHz、Philips I²S、32 位时隙、无 MCLK；LRCK/BCLK/DOUT0 为 PB2/PB3/PB4。采样数组使用 `uint32_t`，PCM16 放在低 16 位，按 L/R 交织；size 为声道采样数。

48 kHz 使用 24.576 MHz 音频源时钟、BCLK /8，每帧 64 位，`CLKD=0x51`。旧版按 294.912 MHz 计算并使用 /96，导致实际约 4 kHz，10 ms 数据块耗时约 120 ms，继而超时。修复应更新 HAL 时钟代码和例程，不应只扩大超时。

tone 一次生成并发送 9600 帧（200 ms），幅度约为满量程的 1/16，首尾各有 5 ms 淡入淡出。整个音调只调用一次 `HAL_I2S_Transmit()`，中途不停止时钟；替换了旧例程反复停启的 10 ms 三角波块，便于听辨音高并减少边界爆音。

| 串口命令 | 输出 |
| --- | --- |
| `p` | 左声道 1 kHz，右声道 500 Hz |
| `l` | 仅左声道 1 kHz，右声道静音 |
| `r` | 仅右声道 500 Hz，左声道静音 |

采样缓冲区占 75 KiB，使用完整 128 KiB SRAM 的 `fel-debug` / `fel-release` 或 DDR 配置；不适用于 32 KiB 的 `fel-direct`。不依赖浮点或运行时三角函数库。

tone 在启动时将 PB5 配为 GPIO 输出高，启用外部功放，等待 10 ms 后接收命令；不使用 PB5 的 MCLK 功能。其他接线请修改或删除这段使能代码。silence 例程不控制功放。

启动打印 `CPU Hz` 和 `CLKD`。发送成功输出 `I2S tone=0`、`elapsed ms`（约 202 ms，含 FIFO 排空及尾部等待）和 `200 ms sine complete.`。发送失败时打印状态、`ErrorCode` 和剩余样本数，并返回命令循环；再次按 `p` / `l` / `r` 会重新初始化 I²S 后重试。`ErrorCode=64` 表示 TX 缺数，`32` 表示 TX 溢出，`256` 表示超时。旧例程的 `I2S tone=1` 会进入 `Error_Handler()` 永久等待，外观上类似卡死；更新例程后可直接看到具体错误并重试。

已在 R328-S3、CPU 408 MHz、FEL SRAM Debug 配置下验证：`CLKD=0x51`，200 ms 正弦波返回 `I2S tone=0`、`elapsed ms=202`，听音恢复正常。其他采样率的分频由主机回归验证，尚未逐一上板测量。

HAL 根据 FIFO 计数批量补充 TX / 读取 RX，每批检查超时，避免每个样本都读取并换算毫秒计数器。低 CPU 时钟及较长中断仍可能影响轮询吞吐，连续播放应使用 DMA。主机回归覆盖时间基准读取期间 FIFO 持续消耗，以及连续 20 个 960 样本块的发送。

音频 PLL 与其他音频用户共享；初始化、分频支持范围和连续传输限制见 [接口文档](../../docs/peripherals.md)。构建见 [例程索引](../README.md)。
