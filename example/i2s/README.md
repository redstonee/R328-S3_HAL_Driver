# I²S / IIS

| 例程 | 预设 | 内容 |
| --- | --- | --- |
| [silence](silence/main.c) | `fel-i2s-silence` | 一次发送 128 帧静音，然后串口回显 |
| [tone](tone/main.c) | `fel-i2s-tone` | 按 p 发送左 1 kHz / 右 500 Hz 的 PCM16 三角波块 |

默认 I²S0 主机发送，48 kHz、Philips I²S、32 位时隙、无 MCLK；LRCK/BCLK/DOUT0 为 PB2/PB3/PB4。采样数组使用 `uint32_t`，PCM16 放在低 16 位，按 L/R 交织；size 为声道采样数。

tone 生成 480 帧（10 ms）立体声数据，幅度约为满量程的 1/16。每次命令发送 20 块，每块之间会停止和重新启动接口，存在时钟间隙和尾部零样本。它用于观察数据、声道和有限块输出，不能用作连续音频播放器。例程不控制外部 DAC 或功放使能。

音频 PLL 与其他音频用户共享；初始化、分频支持范围和连续传输限制见 [接口文档](../../docs/peripherals.md)。构建见 [例程索引](../README.md)。
