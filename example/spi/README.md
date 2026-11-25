# SPI

| 例程 | 内容 |
| --- | --- |
| [clocks](clocks/main.c) | 1 MHz 测试数据，两个片选均保持无效 |
| [loopback](loopback/main.c) | 131 字节跨 FIFO 全双工环回校验 |
| [ili9341](ili9341/README.md) | ILI9341 LCD 与 XPT2046 触摸共用 SPI1 |

默认 SPI1 SCLK/MOSI/MISO 为 PH5/PH6/PH7、AF3；GPIO 片选为 PH4/PH8。构建命令见 [例程索引](../README.md)，HAL 限制见 [接口说明](../../docs/peripherals.md)。

clocks 用于观察时钟和 MOSI；没有器件选中时，MISO 不保证有效。loopback 需要外部短接 MOSI 与 MISO，保持两个设备片选无效，避免其他输出驱动 MISO。每轮改变测试图样，报告不匹配字节数并转储接收数据前 16 字节。131 字节覆盖多个 64 字节 FIFO 批次及尾部短批次。

显示/触摸例程包含设备命令、GPIO 片选、D/C 切换、RGB565 刷色、触摸解码与滤波。整个设备事务由前台串行执行，具体接线与校准见其 [独立说明](ili9341/README.md)。
