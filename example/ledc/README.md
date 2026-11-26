# LEDC / WS2812

`ws2812/main.c` 使用 R328 的硬件 LEDC，通过 PE2（AF2）驱动 4 颗 WS2812。程序循环显示低亮度红、绿、蓝、白色，逐颗流水和蓝色呼吸，并在 PH0/PH1、115200 8N1 串口输出阶段与 tick。

## 复制与运行

先将自己的 `Core/Src/main.c` 备份到 `Core/Src/` 以外，再从工程根目录执行：

```sh
cp example/ledc/ws2812/main.c Core/Src/main.c
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入 BROM FEL 后，仅下载到 SRAM 运行：
cmake --build build/fel-debug --target fel-run
```

xfel 不在 PATH 时，配置命令增加 `-DR328_XFEL=/absolute/path/to/xfel`。同一份应用也可使用 `nand-debug` 或 `nor-debug` 构建；预设只选择内存和加载方式，详见 [例程索引](../README.md)。恢复自己的应用时替换 `Core/Src/main.c` 即可。

## 连接与灯效

引脚、灯数和时序均配置在 `ws2812/main.c` 的 `ledc_init()` 与 `WS2812_LED_COUNT` 中。

| 项目 | 默认值 |
| --- | --- |
| 数据引脚 | PE2，AF2，LEDC_DO |
| 灯珠 | XL-5050RGBC-WS2812B，共 4 颗 |
| 数据链顺序 | PE2 → LED7 → LED6 → LED5 → LED4 |
| 缓冲区格式 | 每灯一个 `uint32_t`，低 24 位为 `0xGGRRBB`，高位先发 |
| 码率 | 800 kbit/s，每位 1.25 µs |
| 0 码 | 高约 292 ns，低约 958 ns |
| 1 码 | 高约 958 ns，低约 292 ns |
| 帧复位低电平 | 300 µs |

测试亮度限制在 32/255 以内。流水顺序按数据链定义，不按器件编号递增。迁移到其他灯链时，同步修改 GPIO 配置、`WS2812_LED_COUNT` 和初始化时序。

## 调用方式

```c
uint32_t pixels[WS2812_LED_COUNT] = {0};
HAL_StatusTypeDef status = ledc_init(WS2812_LED_COUNT);
if (status == HAL_OK)
{
    pixels[0] = HAL_LEDC_ColorRGB(16U, 0U, 0U);
    status = HAL_LEDC_Transmit(&hledc, pixels, WS2812_LED_COUNT);
}
```

`HAL_LEDC_ColorRGB()` 接受常规 RGB 分量，负责转换成 GRB 字。一次发送必须覆盖 `Init.LEDCount` 指定的整条灯链；超时通过 `HAL_LEDC_SetPollingTimeout(ms)` 设置，默认 100 ms，包含发送和最终复位脉冲，后续发送不再传 timeout。成功返回后可以立即发送下一帧。

轮询模式支持 1–1024 颗灯。4 颗灯的数据在启动前全部预填 FIFO；超过 32 颗时由 CPU 持续补充。长时间 IRQ 会影响长灯链补数，连续高负载刷新仍需后续 DMA 实现。FIFO 溢出、缺数或超时后，驱动复位控制器，句柄进入 RESET；检查 `hledc.ErrorCode`，重新初始化后才能重试。接口细节见 [外设说明](../../docs/peripherals.md#ledc)。

## 验证

已通过 FEL SRAM 实板验证：4 颗灯的颜色、流水顺序、蓝色呼吸和串口 tick 均正常。测试程序下载后完成回读比较，未擦写 Flash。长灯链和 LEDC DMA / 中断方式尚未实板验证。

主机 `ledc_register_logic` 测试覆盖时钟、引脚、时序编码、32 字 FIFO 边界、1024 灯帧、重复发送、超时回绕和错误恢复。全部 14 项主机测试通过；例程另通过 FEL Debug / Release 和 NAND Debug 构建及产物检查。

时序依据 [XL-5050RGBC-WS2812B 规格书](https://atta.szlcsc.com/upload/public/pdf/source/20230111/86AAD2096485A2E5BCAB883FF8CBE6FD.pdf)，LEDC 寄存器依据 R328-S3 User Manual V1.4 第 6.10 节。
