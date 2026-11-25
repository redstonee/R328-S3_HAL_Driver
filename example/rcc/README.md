# RCC 时钟查询

将 [clocks/main.c](clocks/main.c) 复制到 `Core/Src/main.c`，并按 [例程索引](../README.md) 复制公共文件。每秒更新并输出 CPU、APB2、架构计数器和音频 PLL4X 频率，以及 CPUX / APB2 原始配置寄存器。

公共初始化先将 APB2 配置为 OSC24M；该例程不重配 CPU PLL 或音频 PLL。CPU 时钟源或分频组合不受查询实现支持时返回 0；音频 PLL 寄存器组合未被查询实现识别时也返回 0。输出的是配置推导值，不是外部测量结果。

构建步骤见 [例程索引](../README.md)。
