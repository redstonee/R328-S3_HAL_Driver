# R328 启动 prebuilt

只保留一个二进制：`boot0-spinand.bin`，32,768 字节。它包含 R328 的原厂 DDR 训练和 NAND 读取实现，供 SPI NAND / SPI NOR 冷启动复用。代码为 ARM / Thumb 混合，链接在 SRAM `0x20000`，版本标识 `553a5eb`。

`spinand.json` 保存 MX35LF1GE4AB（128 MiB，ID `C2 12`）配置、DDR 参数摘要和来源哈希，原 FS35 配置保存在 `spinand-fs35.json`；`spinor.json` 保存 W25Q64 NOR 布局及同一 DDR 参数摘要。原始系统镜像不参与构建，也不随工程保留。FES1、U-Boot、OP-TEE、DTB、rootfs 均不需要。

| 项目            | 值                                                                 |
| --------------- | ------------------------------------------------------------------ |
| 原始组件        | `boot0_nand.fex`，原容器偏移 `0x2B000`                             |
| boot0 SHA256    | `7d67bfbfd681b5fe65a5491bba7c357d33fed362a8f37470e06a6f928c8e38ad` |
| 来源镜像 SHA256 | `ab89ca3410579d572dd1c220e39cad5f5b8427b4ba2733550577b158880d7f5c` |
| DDR 参数        | 原文件 `0x38–0xB7`，32 个小端参数字                                |
| NAND 参数       | 原文件 `0x1F8` 起 96 字节为空，由打包脚本填写                      |

本文件按原样保存。NAND 打包仅修改介质字段、UART、NAND 参数和 checksum；NOR 打包还将 `0x5564–0x60BF` 的 NAND 读取区域替换为从源码构建的读取器，并在 `0x60C0` 注入 8 字节 Thumb 尾调用。两种方式均保留 DDR 参数和训练代码。

本 prebuilt 来自用户提供的固件，原有权利和许可条款继续适用；工程源码的许可证不覆盖该原厂二进制。这里的来源记录不构成对外分发授权。

该版本二进制的直接启动 ABI（文件内偏移，实际地址加 `0x20000`）：

- `0x60C0`：按启动介质选择 NAND 读取器，将 TOC1 读入 `0x42E00000` 并校验。
- `0x6114`：解析 TOC1，读取每个负载 `+0x2C` 的目的地址，按 512 字节扇区复制。
- `0x628C`：识别 `freertos` 条目，将目的地址记作直接应用入口。
- `0x6414–0x6416`：直接入口非零时跳过 `0x6418–0x6476` 的 U-Boot 头部修改。
- `0x6478`：关闭 MMU/cache；`0x64A8–0x64B4` 选择直接入口并跳转。

这些偏移只适用于上述 SHA256 的 prebuilt。QEMU 测试执行此路径，但不验证 DRAM / NAND MMIO。构建、布局和烧写约定见 [NAND 文档](../../docs/nand-image.md) 和 [W25Q64 NOR 文档](../../docs/nor-image.md)。MX35LF1GE4AB 的 USB 烧写步骤见 [xfel NAND 烧写](../../docs/fel-nand.md)。

R328 实板 ROM 确认 boot0 使用完整 **2 KiB 主数据页**，前 8 块内的副本放在块 **0、2、4、6**。不要使用旧版每页 1 KiB 的布局；它虽能回读一致，却会在 ROM 校验失败后返回 FEL。修正布局的 MX35LF1GE4AB 镜像已通过断电冷启动，应用 tick 正常，见 [排查与验证记录](../../docs/nand-boot-debug.md)。原始 boot0 prebuilt 没有修改。
