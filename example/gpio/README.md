# GPIO

| 例程 | 内容 |
| --- | --- |
| [output](output/main.c) | 先预置输出值，再配置输出；循环置位、复位、翻转，每次回读 |
| [input](input/main.c) | 上拉输入，仅在电平稳定 20 ms 后报告变化 |
| [interrupt](interrupt/main.c) | 双边沿中断、事件记录、前台消抖；按 q 释放中断 |

将所选 `main.c` 复制到 `Core/Src/`，将 `gpio_example.h` 复制到 `Core/Inc/`；公共文件和构建步骤见 [例程索引](../README.md)。

## 引脚与输入信号

复制后的 `Core/Inc/gpio_example.h` 默认输出 PB6、输入 PB7。输出可接适当负载观察；输入可通过开关接地，上拉时释放为高电平。修改端口或引脚时同步检查复用冲突。

输入示例对候选电平进行稳定时间判断。中断示例使用真实 EINT，每个原始边沿均计数；前台等最后一个边沿后安静 20 ms 才报告当前电平。这是事件聚合，不把 raw edges 当作按键次数。

## EXTI 接口

```c
static const GPIO_EXTI_InitTypeDef input = {
    .Pin = GPIO_PIN_7,
    .Trigger = GPIO_EXTI_BOTH,
    .Pull = GPIO_PULLUP,
};
HAL_StatusTypeDef status = HAL_GPIO_EXTI_Init(GPIOB, &input);
```

先完成 `HAL_Init()`，再调用 `HAL_GPIO_EXTI_Init()`。HAL 自动注册对应端口的 GIC 分发，EINT 引脚复用为 AF6。用户实现 `HAL_GPIO_EXTI_Callback(GPIO_TypeDef *port, uint32_t pin)`；每个 pending 引脚分别回调，回调在 IRQ 上下文执行。

支持 PB0–13、PE0–6、PG0–14、PH0–9；PC 不支持此接口。触发方式为 `GPIO_EXTI_RISING/FALLING/BOTH/HIGH/LOW`。电平触发需要在返回 IRQ 前消除或屏蔽有效电平，否则会持续触发。

端口共享一条 GIC IRQ：PB=75、PE=76、PG=78、PH=79。多个引脚可分别初始化，最后一个引脚释放时才注销该端口的 IRQ。已经启用的同一引脚再次初始化返回 `HAL_BUSY`；未托管的活动引脚或 GIC 处理函数冲突也返回 `HAL_BUSY`，避免接管已有使用者。

中断引脚应先调用 `HAL_GPIO_EXTI_DeInit()`，再用普通 GPIO API 改变复用。初始化和释放放在前台；IRQ 回调只记录计数、标志或时间，不进行延时和串口输出。

每个端口采样时钟初始化为 HOSC24M / 1，未提供硬件慢采样消抖配置。示例消抖由应用完成。Non-secure 加载时，安全固件还需把对应 GIC IRQ 分配到 Group 1，并开放 GPIO 访问。

寄存器依据为 R328-S3 User Manual V1.4 第 6.6 节的 PB/PE/PG/PH EINT 明细。主机测试检查共享 IRQ、无效参数、冲突、W1C 写入掩码和回调分发；实际边沿采样与电气行为需硬件验证。
