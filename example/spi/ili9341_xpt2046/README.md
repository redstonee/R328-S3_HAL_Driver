# ILI9341 + XPT2046

显示和电阻触摸共用 SPI1。程序初始化 LCD，依次进行红、绿、蓝清屏，显示八色色条和白色边框，随后读取触摸并绘制白色轨迹。UART 输出原始 12 位坐标和屏幕坐标，发送 `c` / `C` 恢复色条；`p` / `i` / `d` 切换轮询、中断和 DMA。

同类传输示例见 [SPI 模块](../README.md)。

## 示例引脚

以下为代码中的默认配置，移植时同步修改 MSP、BSP 片选和设备控制引脚：

| 信号 | 默认配置 |
| --- | --- |
| SCLK / MOSI / MISO | PH5 / PH6 / PH7，AF3，两器件共享 |
| LCD CS | PH4，GPIO，低有效 |
| Touch CS | PH8，GPIO，低有效 |
| LCD RESET | PH2，GPIO，低有效 |
| LCD D/C | PH3，GPIO，0 为命令、1 为数据 |
| 背光控制 | PH9，GPIO，高有效 |
| Touch PENIRQ | PB13，输入上拉，低有效 |

UART0 默认 PH0/PH1。本例不读取 LCD ID 或 GRAM，LCD MISO 无需连接；共享 MISO 用于 XPT2046 DOUT。XPT2046 BUSY 未使用。

## 复制、构建与运行

从工程根目录执行，先备份自己的 `Core/Src/main.c`；切换例程时先移出前一例程的附加文件。

```sh
cp example/common/example.c Core/Src/
cp example/common/example.h Core/Inc/
cp example/spi/ili9341_xpt2046/main.c Core/Src/main.c
cp example/spi/ili9341_xpt2046/ili9341.c example/spi/ili9341_xpt2046/xpt2046.c Core/Src/
cp example/spi/ili9341_xpt2046/ili9341.h example/spi/ili9341_xpt2046/xpt2046.h Core/Inc/
cp example/spi/ili9341_xpt2046/display_bus.c Core/Src/
cp example/spi/ili9341_xpt2046/display_bus.h Core/Inc/
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入 BROM FEL 后执行：
cmake --build build/fel-debug --target fel-run
```

公共文件、切换与恢复说明见 [例程索引](../../README.md)。复制后修改 `Core` 内的设备文件即可。xfel 不在 PATH 时，在配置命令中增加 `-DR328_XFEL=/absolute/path/to/xfel`。

## 显示与共享总线

显示配置为竖屏 240×320、RGB565、`MADCTL=0x48`，像素高字节先发。使用 LCD 自带 GRAM，CPU 仅保留 256 字节刷色缓冲，不分配全屏 framebuffer。复位、Sleep Out、Display On 和黑色清屏成功后开启背光；面板参数可在 `ili9341.c` 的 setup 表中调整。

两器件使用 **SPI mode 0、MSB first**，LCD 默认 **10 MHz**，触摸保持 **2 MHz**。`Display_SPI_Select()` 先释放两个片选，再通过 `HAL_SPI_SetBaudRate()` 切换速率，最后选中目标器件；传输完成后释放片选，错误路径也会释放。一次 LCD 事务内保持速率不变，事务仅在单一前台顺序执行，不从 IRQ 插入调用。触摸响应会等待当前刷屏结束，启动清屏期间不采样。

LCD 速率在 `ili9341.h` 的 `ILI9341_SPI_HZ` 配置，触摸速率在 `xpt2046.h` 的 `XPT2046_SPI_HZ` 配置。启动串口分别打印两个设备的实际速率。R328 支持最高 100 MHz SCLK，HAL 已支持 PLL 时钟源；ILI9341 手册 18.3.4 规定写时钟周期至少 100 ns，即 10 MHz，因此默认值取 10 MHz。若要试 20 / 25 / 50 MHz，可修改 LCD 宏，但这些属于超出屏幕手册时序的配置，需实测花屏、边框与连续刷色；触摸仍维持 2 MHz。分频不一定能精确满足请求，实际值向下选择，见 `hspi1.ActualBaudRateHz`。

XPT2046 使用 24 时钟收发，切换通道后丢弃第一次结果，再取五次采样中值。控制字采用差分模式、`PD1:0=00`，保持空闲时 PENIRQ 可用；未按下时不访问 SPI。

SPI 返回成功仅代表控制器完成传输，实际显示效果需观察屏幕确认。

## 轮询、中断与 DMA

启动默认轮询。串口命令 `p` 为轮询、`i` 为中断、`d` 为 DMA；每次切换后重新绘制色条并输出 `Color bars complete`，随后触摸画点也使用选定模式。`c` / `C` 在当前模式下重绘色条。

`display_bus.c` 将 LCD 的发送和触摸的全双工收发路由到对应 HAL API。HAL 的 `_IT` / `_DMA` 启动后立即返回；设备辅助层调用 `HAL_SPI_Process()` 等待完成回调，期间保持片选与 D/C，局部缓冲区持续有效。因此例程整体仍按前台顺序执行，适合检查传输正确性，不演示后台并行绘图。

DMA 使用通道 0（TX）、1（RX）与 SPI1 请求 23。LCD 的 256 字节像素块跨越 64 字节 FIFO，触摸的 3 字节同时收发检查短包和尾部处理；DMA 模式不回退到轮询。中断模式按 64 字节分批，DMA 每块使用一次交换。请保持启动代码关闭 MMU / D-cache，并避免其他外设复用这两个 DMA 通道。

已通过 FEL SRAM Debug 镜像实板验证：LCD 10 MHz / 触摸 2 MHz，启动串口分别输出 `LCD actual SPI Hz=10000000` 与 `Touch actual SPI Hz=2000000`。依次发送 `p`、`i`、`d`，轮询、中断和 DMA 三种模式的色条、边框与触摸画点均正常。验证包含 LCD 的 256 字节发送、触摸的 3 字节全双工收发及设备间速率切换。此前 LCD / 触摸均为 2 MHz 的配置也已通过相同显示与触摸检查。

## 触摸校准

`xpt2046.h` 中的 `TOUCH_RAW_X/Y_MIN/MAX` 为初始估计值。按压四边附近，观察串口原始值并修改边界；驱动先归一化两个通道，再应用轴交换和反向，最后映射到 240×320 并限幅。

- 横纵轴对调时修改 `TOUCH_SWAP_XY`，当前默认开启。
- 左右或上下反向时修改 `TOUCH_INVERT_X/Y`。
- 映射偏移或比例不合适时调整对应原始值边界。

方向取决于触摸电极接线、面板安装和 LCD 扫描方向。初始偏移或镜像不一定是 SPI 故障。

## 排查与验证

白屏时检查背光、复位、D/C、CS 和 SPI 信号；触摸无响应时检查 PENIRQ 是否随按压变低，以及 Touch CS / MISO。SPI 失败后本例关闭背光并停在 `Error_Handler`，需重新加载。

主机协议模型覆盖三种传输模式下的 RGB565 字节序、像素数量、矩形裁剪、触摸解码与滤波、片选释放后的独立速率切换，以及切换失败时不选中设备，不模拟面板供电或触摸校准。SPI 寄存器模型另验证真实 HAL 的分频、PLL 锁定与失败处理、FIFO、DMA 描述符、IRQ 完成顺序、超时与中止。

寄存器和时序依据：[ILI9341 手册](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf)、[XPT2046 手册](https://files.waveshare.com/upload/9/98/XPT2046-EN.pdf)。例程不依赖 Arduino 或图形库。
