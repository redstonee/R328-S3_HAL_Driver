# UART

| 例程 | 内容 |
| --- | --- |
| [echo](echo/main.c) | 轮询读取单字节并回显 |
| [line](line/main.c) | 有界行缓冲、退格、换行识别、溢出与空闲超时 |
| [interrupt](interrupt/main.c) | IT 定长收发，双缓冲全双工回显 |
| [dma](dma/main.c) | DMA 定长收发，独立 TX/RX DMA 通道与双缓冲回显 |

各例程在各自 `main.c` 中显式配置 UART0 PH0 TX / PH1 RX、115200 8N1；更换串口时修改句柄实例和 GPIO 复用，框架不预留 console。构建步骤见 [例程索引](../README.md)。

行输入接收可打印 ASCII，最多 63 字节。CR、LF、CRLF 均结束一行；Backspace / DEL 删除最后一个字符。超长行一直消费到终止符后整行丢弃；未完成输入空闲 5 s 后丢弃。示例输出行长度和内容，不依赖字符串库或动态内存。

`echo/line` 先用 `HAL_UART_GetRxFifoLevel()` 检查是否有输入，再调用 Receive，空闲时不阻塞主循环。UART 全局轮询期限在初始化时设为 100 ms，收发与文本输出共用。接收错误打印 `ErrorCode`，丢弃当前部分行。

## 文本输出

输出函数位于 UART HAL，不需要额外公共文件。使用已初始化的句柄；轮询超时只需设置一次：

```c
HAL_UART_SetPollingTimeout(100U); /* UART0–3 share this policy; IT/DMA unchanged */
HAL_UART_PrintString(&huart0, "counter=");
HAL_UART_PrintU32(&huart0, 123U);
HAL_UART_PrintString(&huart0, " hex=0x");
HAL_UART_PrintHex(&huart0, 0xABCDU); /* 0000ABCD */
HAL_UART_PrintString(&huart0, "\r\n");
HAL_UART_PrintValue(&huart0, "tick=", HAL_GetTick()); /* label + decimal + CRLF */
HAL_UART_HexDump(&huart0, bytes, sizeof(bytes)); /* AA BB ... + CRLF */
```

`PrintU32` 输出无符号十进制；`PrintHex` 固定输出 8 位大写十六进制，不加前缀或换行。`HexDump` 每字节后加一个空格，最后加 CRLF。所有 UART 实例共用 `HAL_UART_SetPollingTimeout(ms)` 的配置，默认 100 ms；Init / DeInit 不重置设置。0 不等待未就绪条件，`HAL_MAX_DELAY` 不限时。每次函数调用固定本次期限，并共用一个句柄锁，失败返回原 HAL 状态；多个调用组成的一整行由应用自行保证不被其他上下文插入。接口不初始化 UART、不分配堆内存，不自动绑定标准输出。空指针字符串 / 标签、非空长度配空数据指针返回 `HAL_ERROR`。

## 中断与 DMA 收发

选择 `interrupt/main.c` 或 `dma/main.c`，只需将它复制为 `Core/Src/main.c`，无需额外文件或 CMake 预设。例如：

```sh
# 先把原 main.c 备份到 Core/Src 以外
cp example/uart/dma/main.c Core/Src/main.c
cmake --preset fel-ddr-debug
cmake --build --preset fel-ddr-debug
# 按住 FEL 键复位，进入 FEL 后立即松开
cmake --build build/fel-ddr-debug --target fel-run
```

SRAM 运行可改用 `fel-debug`。串口为 PH0 TX / PH1 RX，115200 8N1。关闭终端本地回显，不自动追加 CR/LF，发送 **恰好 8 字节**，例如 `12345678`；应收到相同的 8 字节。可以重复发送不同内容。少于 8 字节时会继续等待，换行也是数据；这两个例程不把空闲或换行当作帧结束。

RX 使用两个静态缓冲区轮换：本块接收完成、上一块 TX 完成后，在另一个缓冲区重新接收，同时发送本块内容。只在对应完成回调后复用缓冲区。此例用于交互验证，不保证无流控连续流输入时不会溢出。

IT 例程遇到错误会打印首个错误的 LSR、FIFO 字节数、剩余接收长度、PH1 电平和 UART 配置，再停止。`ErrorCode=24`（`0x18`）是帧错误 FE 与 Break BI，表示接收器报告无效停止位和持续低电平，不是少于 8 字节或按键间隔过长。PH1 电平是在错误回调时采样，不能单凭该值判断整个字符的波形；排查时应结合原始 LSR、FIFO 快照及轮询例程的对照结果。

四个入口是 `HAL_UART_Transmit_IT/Receive_IT/Transmit_DMA/Receive_DMA`。IT 与 DMA 均由实际中断 / DMA 搬运数据并完成传输，主循环无需调用 `HAL_UART_Process()`。`TxCpltCallback/RxCpltCallback/ErrorCallback` 通常在 UART / DMA 中断中执行，已经解锁，可在完成回调中重新启动传输；例程回调仅保存标志或错误快照，打印与数据处理放在主循环。Abort、可选 Process 或另一方向轮询收尾时也可能在调用者上下文回调。

异步 TX 在全部数据搬入 FIFO 且 FIFO 空时完成，随后可以复用缓冲区并再次发送。最后一个字符可能尚未发完；更改串口配置、RS485 方向或关闭 UART 前，在前台调用 `HAL_UART_FlushTx()` 等待线路发送完毕，使用 UART 模块轮询期限。阻塞发送仍等待 FIFO 和移位寄存器均空。

DMA 例程分别占用通道 0（TX）和 1（RX），请求号为 `DMA_REQUEST_UART0`。更换 UART 时同步修改实例、GPIO 复用和两个请求号。DMA 句柄 / 描述符和收发缓冲区须持续有效，位于物理 SRAM 或 DDR，沿用框架关闭 MMU / D-cache 的启动条件。两个方向使用不同缓冲区与 DMA 通道；不要与其他外设共享正在工作的通道。

例程不调用 Process，正常收发不执行软件超时，可以一直等待人工输入。需要有限期限时，在前台定期调用 `HAL_UART_Process()`，并通过 `HAL_UART_SetAsyncTimeout()` 在两个方向均空闲时修改期限；默认 100 ms，`HAL_MAX_DELAY` 关闭超时。`HAL_UART_AbortTransmit/AbortReceive` 可分别停止一个方向，`HAL_UART_Abort` 停止两个方向，以 `ErrorCallback` 报告 `HAL_UART_ERROR_ABORT`。若 DMA 停止超时，缓冲区仍被占用，须继续 Process 或等待中止成功后再复用。错误回调也须结合 `State` 检查各方向是否已经停止。

当前支持定长单次传输，尚无 Receive-to-IDLE 或循环 DMA。主机模型已检查不调用 Process 的 IT/DMA 混合全双工、短包 / FIFO 边界、FIFO 空但移位寄存器仍忙时再次发送、显式等待线路发送完毕、回调内重启收发、慢速键入与字符超时 IRQ、LSR 错误快照、计数回绕和中止恢复。

此前的前台完成版本已通过 UART0 IT SRAM 实板验证：PH0/PH1、115200 8N1 下逐字输入 `12345678` 与 `abcdefgh`，两组均正常完整回显，无 FE/BI 错误。当前中断完成、FIFO 空释放 TX 的版本已通过主机模型测试，尚待实板验证；UART DMA 收发也尚待实板验证。
