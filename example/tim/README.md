# Timer

| 例程 | 预设 | 内容 |
| --- | --- | --- |
| [periodic](periodic/main.c) | `fel-tim-periodic` | Timer1 每秒 IRQ，前台输出事件总数与 tick |
| [oneshot](oneshot/main.c) | `fel-tim-oneshot` | 250 ms 单次 IRQ，按 s 再次启动 |

两个例程都使用 Timer1，IRQ 回调只更新事件计数。周期例程有持续中断源，可使用 WFI；单次例程到期后继续轮询串口，不依靠已经停止的定时器唤醒。

`Prescaler` 是 `/1…/128` 的编码，`Period` 为间隔 tick 数：`周期秒数 = Period × 实际分频 / 24000000`。单次例程在重新启动前调用 Stop，满足停止到重装的等待要求；到期后不会自动重复。

`HAL_TIM_Base_Start_IT()` 自动注册 GIC 处理函数，不需要另外编写 IRQ 包装。Non-secure 运行时，安全固件须开放 Timer1 的 GIC IRQ 118。Timer0 也可使用，HAL 时基不占用它。

构建和下载见 [例程索引](../README.md)。
