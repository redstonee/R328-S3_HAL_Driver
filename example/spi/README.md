# SPI

| 例程 | 内容 |
| --- | --- |
| [clocks](clocks/main.c) | 1 MHz 测试数据，两个片选均保持无效 |
| [loopback](loopback/main.c) | 131 字节跨 FIFO 全双工环回校验 |
| [interrupt](interrupt/main.c) | 通用 `_IT` 单向收发、全双工环回、回调与中止 |
| [dma](dma/main.c) | 通用 `_DMA` 单向收发、全双工环回、DMA 连接与错误恢复 |
| [ili9341_xpt2046](ili9341_xpt2046/README.md) | ILI9341 LCD 与 XPT2046 共用 SPI1，轮询 / IRQ / DMA 切换 |

默认 SPI1 SCLK/MOSI/MISO 为 PH5/PH6/PH7、AF3；GPIO 片选为 PH4/PH8。构建命令见 [例程索引](../README.md)，HAL 限制见 [接口说明](../../docs/peripherals.md)。

clocks 用于观察时钟和 MOSI；没有器件选中时，MISO 不保证有效。loopback 需要外部短接 MOSI 与 MISO，保持两个设备片选无效，避免其他输出驱动 MISO。每轮改变测试图样，报告不匹配字节数并转储接收数据前 16 字节。131 字节覆盖多个 64 字节 FIFO 批次及尾部短批次。

## 通用中断与 DMA

`interrupt` 与 `dma` 为两个独立例程，只需复制对应 `main.c` 和公共 `example.c/h`，不依赖 LCD / 触摸设备文件：

```sh
cp example/common/example.c Core/Src/
cp example/common/example.h Core/Inc/
cp example/spi/interrupt/main.c Core/Src/main.c
# DMA 版本改为：cp example/spi/dma/main.c Core/Src/main.c
```

默认 SPI1、mode 0、1 MHz、MSB first，传输 131 字节。短接 **PH6 MOSI 与 PH7 MISO**，两个设备的 GPIO 片选始终保持高电平；其他器件不得驱动共享 MISO。

| 串口命令 | 操作与预期结果 |
| --- | --- |
| `t` | `HAL_SPI_Transmit_IT/DMA`，发送变化的测试图样，只检查完成回调 |
| `r` | `HAL_SPI_Receive_IT/DMA`，硬件发送 `0xFF` 产生时钟，环回接收应全部为 `0xFF` |
| `x` | `HAL_SPI_TransmitReceive_IT/DMA`，校验整个 131 字节图样，预期 `mismatched bytes=0` / `Loopback PASS` |
| `a` | 中止当前传输，清理后通过错误回调报告 `HAL_SPI_ERROR_ABORT` |

主循环持续调用 `HAL_SPI_Process()` 并读取串口；完成和错误回调只记录标志，前台打印结果、耗时、`callback complete`、错误码和接收数据前 16 字节。`start=0` 表示已启动，`result=0` 和 `callback complete=1` 表示异步传输完成；接收成功与环回数据匹配分别报告。BUSY 时拒绝新传输，错误导致 RESET 时在下一条传输命令重新初始化。`a` 在空闲时返回 BUSY；131 字节传输很短，手动发送中止命令前通常已完成。

中断版本直接使用 `_IT` API；DMA 版本显式初始化并连接通道 0（TX）、1（RX），SPI1 请求号 23。两者均使用独立、静态的 TX / RX 缓冲区，在最终回调前保持有效，中止尚未完成时也不复用。保持工程默认 MMU / D-cache 关闭；HAL 自动注册 SPI / DMA 的 GIC 分发，无需修改 `r328_it.c`。

这些例程用于无片选的外部环回。连接实际从机时，在启动前选中设备，最终完成或错误清理结束后释放片选，并按器件协议准备数据；不要在 `_IT` / `_DMA` 启动返回后立刻释放片选。

这两个例程已进行交叉构建与镜像布局检查，尚未单独实板验证；显示 / 触摸的三种模式验证记录见其设备例程。

显示/触摸例程包含设备命令、GPIO 片选、D/C 切换、RGB565 刷色、触摸解码与滤波。整个设备事务由前台串行执行，具体接线与校准见其 [独立说明](ili9341_xpt2046/README.md)。
