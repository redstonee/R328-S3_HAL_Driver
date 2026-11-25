# ILI9341 + XPT2046

显示和电阻触摸共用 SPI1。程序初始化 LCD，依次进行红、绿、蓝清屏，显示八色色条和白色边框，随后读取触摸并绘制白色轨迹。UART 输出原始 12 位坐标和屏幕坐标，发送 `c` / `C` 恢复色条。

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
cp example/spi/ili9341/main.c Core/Src/main.c
cp example/spi/ili9341/ili9341.c example/spi/ili9341/xpt2046.c Core/Src/
cp example/spi/ili9341/ili9341.h example/spi/ili9341/xpt2046.h Core/Inc/
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入 BROM FEL 后执行：
cmake --build build/fel-debug --target fel-run
```

公共文件、切换与恢复说明见 [例程索引](../../README.md)。复制后修改 `Core` 内的设备文件即可。xfel 不在 PATH 时，在配置命令中增加 `-DR328_XFEL=/absolute/path/to/xfel`。

## 显示与共享总线

显示配置为竖屏 240×320、RGB565、`MADCTL=0x48`，像素高字节先发。使用 LCD 自带 GRAM，CPU 仅保留 64 字节刷色缓冲，不分配全屏 framebuffer。复位、Sleep Out、Display On 和黑色清屏成功后开启背光；面板参数可在 `ili9341.c` 的 setup 表中调整。

两器件统一使用 **SPI mode 0、2 MHz、MSB first**。一次事务只选中一个器件，命令和数据传输完成后释放片选，错误路径也会释放。事务仅在单一前台顺序执行，不从 IRQ 插入调用。触摸响应会等待当前刷屏结束，启动清屏期间不采样。

XPT2046 使用 24 时钟收发，切换通道后丢弃第一次结果，再取五次采样中值。控制字采用差分模式、`PD1:0=00`，保持空闲时 PENIRQ 可用；未按下时不访问 SPI。

SPI 返回成功仅代表控制器完成传输，实际显示效果需观察屏幕确认。

## 触摸校准

`xpt2046.h` 中的 `TOUCH_RAW_X/Y_MIN/MAX` 为初始估计值。按压四边附近，观察串口原始值并修改边界；驱动先归一化两个通道，再应用轴交换和反向，最后映射到 240×320 并限幅。

- 横纵轴对调时修改 `TOUCH_SWAP_XY`，当前默认开启。
- 左右或上下反向时修改 `TOUCH_INVERT_X/Y`。
- 映射偏移或比例不合适时调整对应原始值边界。

方向取决于触摸电极接线、面板安装和 LCD 扫描方向。初始偏移或镜像不一定是 SPI 故障。

## 排查与验证

白屏时检查背光、复位、D/C、CS 和 SPI 信号；触摸无响应时检查 PENIRQ 是否随按压变低，以及 Touch CS / MISO。SPI 失败后本例关闭背光并停在 `Error_Handler`，需重新加载。

主机协议模型覆盖 RGB565 字节序、像素数量、矩形裁剪、触摸解码与滤波、事务切换和错误时释放片选，不模拟面板供电或触摸校准。

寄存器和时序依据：[ILI9341 手册](https://cdn-shop.adafruit.com/datasheets/ILI9341.pdf)、[XPT2046 手册](https://files.waveshare.com/upload/9/98/XPT2046-EN.pdf)。例程不依赖 Arduino 或图形库。
