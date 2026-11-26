# UART

| 例程 | 内容 |
| --- | --- |
| [echo](echo/main.c) | 轮询读取单字节并回显 |
| [line](line/main.c) | 有界行缓冲、退格、换行识别、溢出与空闲超时 |

这两个例程在各自 `main.c` 中显式配置 UART0 PH0 TX / PH1 RX、115200 8N1；更换串口时修改句柄实例和 GPIO 复用，框架不预留 console。构建步骤见 [例程索引](../README.md)。

行输入接收可打印 ASCII，最多 63 字节。CR、LF、CRLF 均结束一行；Backspace / DEL 删除最后一个字符。超长行一直消费到终止符后整行丢弃；未完成输入空闲 5 s 后丢弃。示例输出行长度和内容，不依赖字符串库或动态内存。

接收使用 0 ms 超时进行立即轮询；发送使用有限超时并检查返回值。接收错误打印 `ErrorCode`，丢弃当前部分行。UART 中断和 DMA 接收尚未实现。

## 文本输出

输出函数位于 UART HAL，不需要额外公共文件。使用已初始化的句柄与毫秒超时：

```c
HAL_UART_PrintString(&huart0, "counter=", 100U);
HAL_UART_PrintU32(&huart0, 123U, 100U);
HAL_UART_PrintString(&huart0, " hex=0x", 100U);
HAL_UART_PrintHex(&huart0, 0xABCDU, 100U); /* 0000ABCD */
HAL_UART_PrintString(&huart0, "\r\n", 100U);
HAL_UART_PrintValue(&huart0, "tick=", HAL_GetTick(), 100U); /* label + decimal + CRLF */
HAL_UART_HexDump(&huart0, bytes, sizeof(bytes), 100U); /* AA BB ... + CRLF */
```

`PrintU32` 输出无符号十进制；`PrintHex` 固定输出 8 位大写十六进制，不加前缀或换行。`HexDump` 每字节后加一个空格，最后加 CRLF。每次函数调用共用一个句柄锁和超时期限，失败返回原 HAL 状态；多个调用组成的一整行由应用自行保证不被其他上下文插入。接口不初始化 UART、不分配堆内存，不自动绑定标准输出。空指针字符串 / 标签、非空长度配空数据指针返回 `HAL_ERROR`。
