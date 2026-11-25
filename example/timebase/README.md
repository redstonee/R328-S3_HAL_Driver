# 架构计数器与超时

| 例程 | 内容 |
| --- | --- |
| [counter](counter/main.c) | 关 IRQ 延时 1000 ms，输出毫秒差值和原始 64 位计数 |
| [timeout](timeout/main.c) | 不使用 HAL_Delay，调度 250 ms 和 1 s 两个任务 |

counter 用于观察时基不依赖周期 IRQ；timeout 在等待任务到期时仍处理串口回显。任务每次运行后以当前时间重新开始计时，不补跑积压任务。

超时采用 `(uint32_t)(now - start)` 无符号差值，可跨 `HAL_GetTick()` 的 32 位回绕；单次间隔须小于完整回绕周期。原始计数通过一次 64 位读取获得，再拆成高低十六进制输出。

BROM / loader 必须保持 24 MHz CNTPCT 运行并允许当前模式读取。计数器本身不能唤醒 WFI，两个例程均持续轮询。接口和回绕说明见 [时基文档](../../docs/timebase.md)，构建见 [例程索引](../README.md)。
