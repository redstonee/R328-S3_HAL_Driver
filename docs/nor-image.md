# W25Q64 SPI NOR 直接启动

```text
BROM → SRAM boot0：原厂 DRAM 初始化 → SPI0 读取应用包到 DDR
                → 复制应用到 0x40000000 → 关闭 MMU/cache → Reset_Handler
```

W25Q64 容量为 **64 Mbit / 8 MiB**。当前使用标准单线 SPI、模式 0、12 MHz，`03h + 24 位地址` 读取，不需要 Quad Enable 或状态寄存器修改。容量及指令依据 [Winbond W25Q64JV 数据手册](https://media.digikey.com/pdf/Data%20Sheets/Winbond%20PDFs/W25Q64JV_RevK_3-10-21.pdf)。

## 构建

```sh
cmake --preset nor-debug
cmake --build --preset nor-debug
ctest --preset nor-debug
```

体积优化版本使用 `nor-release`。应用仍从 `Core/Src/*.c` 编译，头文件放入 `Core/Inc/`。单独更新镜像可执行 `cmake --build build/nor-debug --target flash-image`。

| 文件（位于 `build/nor-debug/`） | 内容 |
| --- | --- |
| `firmware-nor.bin` | **8 MiB 线性 NOR 镜像**，从 Flash 地址 0 烧入 |
| `boot0.bin` | 32 KiB，保留原厂 DDR 初始化，替换 NAND 读取器并重算 checksum |
| `nor_reader.elf` / `nor-reader.bin` | 从源码构建的 NOR 读取器，含符号 / 可注入代码 |
| `firmware.toc1` | 单个裸机应用的 TOC1 包，按 4 KiB 对齐 |
| `firmware.elf` / `.bin` / `.map` | 应用本身，不能代替完整 Flash 镜像 |
| `firmware-nor.json` | Flash 布局、补丁范围、DDR 配置及各组件 SHA256 |

不需要完整系统镜像、SDK、U-Boot、OP-TEE、DTB 或文件系统。工程只保留原有的一个 32 KiB boot0 二进制，NOR 读取器每次从 [C 源码](../boot/nor/boot0_nor.c) 构建。

## 接线与烧写

NOR 使用 **SPI0 / CS0**，与例程中显示屏 / 触摸所用的 SPI1 独立：

| R328 引脚（AF4） | W25Q64 信号 |
| --- | --- |
| PC0 | CLK |
| PC2 | DI / IO0（MOSI） |
| PC3 | /CS |
| PC4 | DO / IO1（MISO） |

单线模式下 `/WP`、`/HOLD` 保持高电平，电源和 IO 电压按具体料号的数据手册连接。boot0 日志默认使用 **PH0 TX / PH1 RX，115200 8N1**，由 `R328_BOOT_UART0_ROUTE` 控制；应用 UART 独立配置，默认模板无输出。

| Flash 字节偏移 | 内容 |
| --- | --- |
| `0x000000–0x007FFF` | eGON boot0，32 KiB |
| `0x008000–0x00FFFF` | 保留区，填 `0xFF` |
| `0x010000` 起 | TOC1 应用包 |
| 应用包结束至 `0x7FFFFF` | 填 `0xFF` |

使用支持对应 W25Q64 型号的 SPI NOR 烧录器，擦除后将 `firmware-nor.bin` 写到 **地址 0**，回读校验后冷启动。镜像是普通线性地址数据，不带 NAND 页间隔、OOB 或 ECC；不能沿用 NAND 的物理页烧写流程。本布局只有一个 boot0 和应用副本。

也可通过 USB FEL 烧写，无需外接烧录器：进入 BROM FEL 后，先执行 `cmake --build build/nor-debug --target fel-probe` 检测，再执行 `cmake --build build/nor-debug --target fel-flash` 擦除、编程并整片回读校验。**该目标会擦除整颗 8 MiB W25Q64。** 只读校验用 `fel-verify`；具体流程和限制见 [xfel 烧写说明](fel-nor.md)。

R328 手册 3.4.2 说明默认 Try Media 顺序为 SPI NOR → SPI NAND，eFuse 指定启动介质时以芯片配置为准。原版 [xfel 的 R328 后端](https://github.com/xboot/xfel/blob/main/chips/r328.c) 尚未实现 `xfel spinor write`，本工程用原版的 `read / write / exec` 调用 SRAM 烧写程序。`fel-run` 仍只用于 SRAM 应用调试。

## DDR、入口与读取器

DDR 链接窗口为 **128 MiB，`0x40000000–0x47FFFFFF`**。容量按现有硬件信息配置，尚未实测；DDR 参数和训练代码沿用已有 prebuilt，替换 Flash 不会自动适配其他 DDR。

boot0 初始化 DRAM 后，调用源码实现的 NOR 读取器，把 `0x10000` 处的 TOC1 读到 `0x42E00000`，校验长度、单应用条目、目的地址和 checksum。SPI FIFO 错误或轮询超时会返回失败，沿用 boot0 的失败处理，不跳转到应用。读取器不擦写 Flash。

校验成功后沿用 boot0 的 `freertos` 直接应用分支；这个名称只是其 TOC1 分发键，应用不依赖 FreeRTOS。它按 [Flash 启动头部](../Core/Startup/flash_header.S) 中的 `+0x2C` 地址复制应用并直接跳转，维持 Secure SVC、关闭 MMU/D-cache，使用 SECURE GIC Group 0。

应用 BIN 最大 **8,322,560 字节**（`0x7F0000 - 512`），由 Flash 容量、64 KiB 引导保留区和 TOC1 头共同限制；BSS 不占 BIN。暂存包和临时页表在交接后可由应用使用，链接窗口保留完整 128 MiB。

NOR 打包仅接受已核对 SHA256 的原始 boot0。文件内 `0x5564–0x60BF` 原 NAND 读取区域替换为 NOR 源码，`0x60C0` 的 Thumb 分发改为尾调用 `0x25565`；原 DDR 参数、训练代码以及 `0x60DC` 起的 TOC1 复制和主流程保持原样。链接脚本限制读取器范围，并禁止需要初始化的 data/BSS。更换 boot0 版本必须重新核对这些位置，不能直接改哈希绕过检查。

## 独立打包与验证

```sh
python3 tools/nor_image.py pack \
  --reader build/nor-debug/nor-reader.bin \
  --reader-elf build/nor-debug/nor_reader.elf \
  --elf build/nor-debug/firmware.elf \
  --firmware build/nor-debug/firmware.bin \
  --output build/nor-debug/firmware-nor.bin \
  --package build/nor-debug/firmware.toc1 \
  --configured-boot0 build/nor-debug/boot0.bin \
  --manifest build/nor-debug/firmware-nor.json
```

将 `pack` 改为 `verify` 会重建并逐字节检查四个输出，不修改文件。校验包括 prebuilt 哈希、读取器 ELF / Thumb 入口 / BIN 一致性、应用 ELF / BIN / 向量 / 加载地址、eGON 和 TOC1 checksum、容量和整片布局。相同输入可复现输出。

主机测试执行实际 NOR 读取源码，通过寄存器 / FIFO 模型覆盖 `03h` 指令、跨地址边界、坏包、checksum、FIFO 错误、复位与传输超时。可用 QEMU 时另验证注入后的 Thumb 分发、原厂 TOC1 解析、完整应用复制和直接跳转；仿真预装 TOC1 并替换 NOR 读取器入口为成功返回，因此 **不模拟 BROM 读 NOR、真实 SPI 时序或 DDR 训练**。

**目前尚未在 W25Q64 上验证实板冷启动。** 镜像与源码已通过软件校验，首次硬件验证应观察 boot0 的 DRAM 日志和所选例程的输出。必要 prebuilt 的来源与许可见 [说明](../prebuilt/r328/README.md)。
