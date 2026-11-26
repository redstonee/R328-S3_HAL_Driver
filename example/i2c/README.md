# I²C / TWI

| 例程 | 内容 |
| --- | --- |
| [probe](probe/main.c) | 只探测地址 0x50，报告状态和错误码 |
| [scan](scan/main.c) | 扫描非保留地址 0x08–0x77，按 s 再次扫描 |
| [memory](memory/main.c) | 按 r 读取指定寄存器范围并转储数据 |
| [interrupt](interrupt/main.c) | 通用 `_IT` 主机收发、寄存器读写、完成回调与中止 |
| [dma](dma/main.c) | 通用 `_DMA` 主机收发、寄存器读写、DMA 连接与错误恢复 |
| [bmp280](bmp280/README.md) | PB8/PB9、地址 0x76，轮询 / IRQ / DMA 温度与气压测试 |

默认 TWI1 PB8 SCL / PB9 SDA、AF4、100 kHz。外部上拉应按负载和速率配置；本例诊断 UART0 使用 PH0/PH1；若应用将 UART 移到 PB8/PB9，应自行处理引脚冲突。

地址在配置中使用原始 7 位值，调用 HAL 时左移一位。地址 NACK 为设备未响应；扫描遇到 NACK 后继续，其他错误终止本轮。总线错误或超时导致句柄 RESET 时，示例显式重新初始化后等待下一条命令。

memory 例程还需将 `memory/config.h` 复制到 `Core/Inc/`。复制后的 `Core/Inc/config.h` 设置设备地址、起始寄存器、8/16 位寄存器地址宽度和读取长度（1–64 字节）。默认地址 0x50、8 位寄存器 0、8 字节，仅为调用示意，运行前按目标设备选择可读范围。`HAL_I2C_Mem_Read()` 以写方向发送寄存器地址，再 repeated START 转为读取，最后发送 NACK / STOP；示例不改写设备寄存器数据。

## 通用中断与 DMA

`interrupt` 与 `dma` 为两个独立例程，直接展示对应 HAL API，不依赖 BMP280。将选定的 `main.c` 复制到 `Core/Src/`，并把 `i2c/i2c_async_config.h` 复制到 `Core/Inc/`：

```sh
cp example/i2c/interrupt/main.c Core/Src/main.c
# DMA 版本改为：cp example/i2c/dma/main.c Core/Src/main.c
cp example/i2c/i2c_async_config.h Core/Inc/
```

在复制后的配置头文件中设置总线速率、超时、7 位地址、寄存器地址及其宽度、读取长度和发送数据。默认 100 kHz、地址 `0x50`、8 位寄存器 `0x00`、读 8 字节，发送数据为 `55 AA 00 FF`；这些只是协议示意，需按实际从机调整。上电仅初始化，不自动读写。

| 串口命令 | 操作 |
| --- | --- |
| `t` | `Master_Transmit_IT/DMA`，原样发送配置的数据，不附加寄存器地址 |
| `r` | `Master_Receive_IT/DMA`，直接读配置的字节数，不预先写入寄存器地址 |
| `m` | `Mem_Read_IT/DMA`，写寄存器地址后 repeated START 读取 |
| `w` | `Mem_Write_IT/DMA`，在寄存器地址后发送配置的数据 |
| `a` | 中止当前传输，完成清理后通过错误回调报告 `HAL_I2C_ERROR_ABORT` |

主机收发的两次调用是独立的 START / STOP 事务；需要寄存器寻址时使用 `m` / `w`。EEPROM 的页大小和写入等待时间、传感器的命令格式由用户按器件协议处理，例程不自动连续读回或比较任意设备数据。

主循环持续调用 `HAL_I2C_Process()`，同时轮询串口。四种完成回调和错误回调只置标志，前台打印结果、耗时、`callback complete` 和 `I2C ErrorCode`，成功接收后转储数据。`start=0` 只表示已启动，`result=0` 和 `callback complete=1` 才表示异步传输成功。BUSY 时拒绝新命令；发生 RESET 后，下一次传输命令重新初始化并恢复超时设置。`a` 在空闲时返回 BUSY；短传输可能在输入中止命令前已完成。

DMA 版本使用通道 0（TX）、1（RX），TWI1 请求号 46，并显式连接 `hi2c1.hdmatx/hdmarx`。缓冲区为静态可写 SRAM / DDR 对象，直到最终回调前不复用；中止尚未完成时持续调用 Process，保持 BUSY。保持工程默认 MMU / D-cache 关闭。中断和 DMA 的 GIC 分发由 HAL 自动注册，无需改动 `r328_it.c`。

这两个例程已进行交叉构建与镜像布局检查，尚未单独实板验证；HAL 的 BMP280 实板验证记录见其设备例程。

构建和下载见 [例程索引](../README.md)，API 与错误恢复详见 [接口文档](../../docs/peripherals.md)。
