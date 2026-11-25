# I²C / TWI

| 例程 | 预设 | 内容 |
| --- | --- | --- |
| [probe](probe/main.c) | `fel-i2c-probe` | 只探测地址 0x50，报告状态和错误码 |
| [scan](scan/main.c) | `fel-i2c-scan` | 扫描非保留地址 0x08–0x77，按 s 再次扫描 |
| [memory](memory/main.c) | `fel-i2c-memory` | 按 r 读取指定寄存器范围并转储数据 |

默认 TWI1 PB8 SCL / PB9 SDA、AF4、100 kHz。外部上拉应按负载和速率配置；UART0 选择 PB8/PB9 路由时不能同时使用此 TWI1 配置。

地址在配置中使用原始 7 位值，调用 HAL 时左移一位。地址 NACK 为设备未响应；扫描遇到 NACK 后继续，其他错误终止本轮。总线错误或超时导致句柄 RESET 时，示例显式重新初始化后等待下一条命令。

`memory/config.h` 设置设备地址、起始寄存器、8/16 位寄存器地址宽度和读取长度（1–64 字节）。默认地址 0x50、8 位寄存器 0、8 字节，仅为调用示意，运行前按目标设备选择可读范围。`HAL_I2C_Mem_Read()` 以写方向发送寄存器地址，再 repeated START 转为读取，最后发送 NACK / STOP；示例不改写设备寄存器数据。

构建和下载见 [例程索引](../README.md)，API 与错误恢复详见 [接口文档](../../docs/peripherals.md)。
