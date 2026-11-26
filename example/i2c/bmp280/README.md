# BMP280：轮询、中断和 DMA

TWI1 使用 PB8 SCL / PB9 SDA，100 kHz；UART0 为 PH0 TX / PH1 RX，115200 8N1。BMP280 使用 3.3 V 与共地，SDO 接地，地址为 **0x76**；CSB 接 VDDIO 以选择 I²C。SCL/SDA 需要到 3.3 V 的外部上拉，模块上已有上拉时不用重复添加。地址与接口选择依据 [Bosch BMP280 数据手册](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bmp280-ds001.pdf)。

备份当前应用后，从工程根目录复制：

```sh
cp example/common/example.c Core/Src/
cp example/common/example.h Core/Inc/
cp example/i2c/bmp280/main.c Core/Src/main.c
cp example/i2c/bmp280/bmp280.c Core/Src/
cp example/i2c/bmp280/bmp280.h Core/Inc/
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入 BROM FEL 后：
cmake --build build/fel-debug --target fel-run
```

切换到其他例程时移出 `Core/Src/bmp280.c` 和 `Core/Inc/bmp280.h`。不需要增加例程专用预设。

| 命令 | 模式 |
| --- | --- |
| `p` | 轮询寄存器读写 |
| `i` | 传统 TWI engine 中断读写 |
| `d` | TWI packet engine + 系统 DMA 读写 |

每条命令检查 `0xD0` 的芯片 ID（应输出十进制 88，即 `0x58`），复位传感器并等待校准参数加载，读取 `0x88` 开始的 24 字节校准参数，然后以温度/压力各 x1 的 forced 模式测量一次。读取 `0xF7` 开始的 6 字节后，输出摄氏温度与 Pa 气压；三种模式的结果应接近，连续测量会有正常噪声和温漂。

补偿采用数据手册方程，使用 libgcc 软件双精度运算，不依赖 FPU、libm 或浮点 printf。主机已验证校准解析、负数系数、25.08 °C / 100653 Pa 参考向量以及无效采样。

DMA 使用通道 0（TX）、1（RX）与 TWI1 请求 46，`main.c` 中初始化并连接 `hi2c1.hdmatx/hdmarx`。数据缓冲区为静态 SRAM/DDR 对象；寄存器前缀由 CPU 写入 FIFO，载荷由 DMA 传输，1/6/24 字节都走真实 DMA。修改通道时须避开其他 DMA 用户。

`i` / `d` 的 `callback complete` 应为 1。示例在等待结果时调用 `HAL_I2C_Process()`，字节传输由 IRQ 或 DMA 推进；Process 处理最终完成和超时，不逐字节轮询数据。`sample read ms` 是毫秒分辨率的单次读取耗时，不能用于比较短事务的性能。

失败打印 HAL 状态及 `I2C ErrorCode`，需要时重新初始化并等待下一条命令。NACK 为 4，超时为 8，DMA 配置/中止错误为 16。驱动约定、缓冲区生命周期及缓存限制见 [I²C 接口文档](../../../docs/peripherals.md#i²c--twi)。
