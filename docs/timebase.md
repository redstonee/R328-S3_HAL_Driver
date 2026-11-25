# ARM 架构计数器时基

`HAL_GetTick()` 读取 Cortex-A7 物理计数器 `CNTPCT`，返回 `uint32_t` 毫秒。`HAL_InitTick()` 保存应用启动时的计数基准，不清零共享计数器。Timer0 不被 HAL 时基占用。

## 频率与回绕

R328 的系统 timestamp 时钟按 **24 MHz** 换算，每毫秒 24000 周期，独立于 CPU PLL、`SystemCoreClock` 和 APB2。设备常量为 `R328_ARCH_COUNTER_HZ`。

```c
uint64_t elapsed = R328_ArchCounter_Read() - tick_epoch;
return (uint32_t)(elapsed / 24000U);
```

`R328_ArchCounter_Read()` 使用 `ISB` 和 `MRRC p15, 0, ..., c14` 一次读取完整 64 位计数。先做 64 位减法和换算，再截取毫秒低 32 位；原始计数低 32 位约每 179 秒进位，不能只读低位再除。

`HAL_GetTick()` 约每 49.7 天回绕。单次超时区间小于一个完整回绕周期时，使用无符号差值比较：

```c
uint32_t start = HAL_GetTick();
if ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) {
    /* Timeout. */
}
```

## 启动条件

BROM / loader 须启用系统计数器并开放当前模式对 CNTPCT 的访问。`HAL_InitTick()` 通过有限次数轮询确认计数变化，停止计数时返回 `HAL_TIMEOUT`。初始化前 `HAL_GetTick()` 返回 0；重复初始化返回 `HAL_BUSY`，保留原基准。

`CNTFRQ` 是固件写入的频率描述，写它不会改变硬件计数速度。本工程不依赖加载方可能未设置的 CNTFRQ，不改写 timestamp 控制寄存器、比较器或计数值。

Non-secure 运行时，安全固件需保留计数器运行并开放访问；存在 Hypervisor 时还需允许 Non-secure PL1 读取物理计数器。禁止访问可能触发异常或陷入 Hypervisor，无法靠停转检测恢复。GIC 安全组配置与计数器访问权限是独立条件。

## 中断与等待

时基不依赖周期 IRQ。关中断或长时间未读取后，下次读数仍反映硬件经过时间；`HAL_IncTick()` 为兼容空函数，无需从 ISR 调用。

`HAL_Delay()` 是毫秒分辨率的忙等，存在毫秒量化误差，不用于精确外设时序。阻塞操作建议放在前台，避免占用 IRQ 栈或延迟其他中断。

自由运行计数器本身不会唤醒 `WFI`。UART 轮询例程持续轮询；需要低功耗等待或调度节拍时，应另配 UART RX IRQ、Timer0/1 或架构定时器比较中断。当前时基不提供周期回调或深度休眠停表补偿。

## 验证

`hal_timebase_tests` 使用真实 HAL 实现与计数器模型，覆盖停转检测、非零启动基准、低 32 位进位、长时间无 IRQ、毫秒回绕和延时跨回绕。模型不验证实际计数器时钟和访问权限。

寄存器操作参考 Arm [CMSIS-Core Generic Physical Timer](https://arm-software.github.io/CMSIS_6/main/Core_A/group__PL1__timer__functions.html)。
