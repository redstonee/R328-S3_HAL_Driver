# 通过 xfel 烧写 W25Q64

使用原版 [xfel](https://github.com/xboot/xfel) 的 `read / write / exec`，由工程内的 SRAM 程序执行 NOR 指令。**不需要修改或重新编译 xfel，也不需要初始化 DDR。** 这没有补齐上游的 `xfel spinor write` 子命令，烧写入口是本工程的 `fel-flash`。

适用器件为 W25Q64，当前接受 JEDEC ID **`EF 40 17` / `EF 70 17`**。接口为 SPI0 / CS0、12 MHz、模式 0；接线见 [NOR 镜像说明](nor-image.md#接线与烧写)。不适用于 SPI NAND。

## 构建与烧写

```sh
cmake --preset nor-debug
cmake --build --preset nor-debug
ctest --preset nor-debug
```

xfel 不在 PATH 时，在配置命令后增加 `-DR328_XFEL=/absolute/path/to/xfel`。`nor-release` 提供相同烧写目标。

按住 FEL 键复位，进入 **BROM FEL** 后**立即松开 FEL 键**。松开后仍保持 FEL 状态。探测、读取、校验及烧写期间都必须保持按键松开，持续按住会导致 SPI Flash 无法读写。先只检测器件：

```sh
cmake --build build/nor-debug --target fel-probe
```

成功时显示 R328 和 W25Q64 的 JEDEC ID。随后执行：

```sh
cmake --build build/nor-debug --target fel-flash
```

**`fel-flash` 擦除整颗 8 MiB W25Q64，覆盖原有内容。** 它使用当前构建的 `firmware-nor.bin`，完成后逐字节回读校验整片，包括填 `0xFF` 的空白区。显示 `Full NOR readback PASS` 后，保持 FEL 键松开，复位 / 重新上电，验证 boot0 日志及应用行为（默认模板无 UART 输出）。程序不会自行退出 FEL 或启动应用。

只回读校验当前镜像，不擦除、不编程：

```sh
cmake --build build/nor-debug --target fel-verify
```

普通构建与 CTest 均不访问 USB。只有显式执行 `fel-probe`、`fel-flash`、`fel-verify` 才连接设备，其中 `fel-probe` / `fel-verify` 不修改 Flash 内容。

## 独立命令

```sh
python3 tools/fel_nor.py flash \
  --helper-manifest build/nor-debug/fel-nor-helper.json \
  --image build/nor-debug/firmware-nor.bin \
  --manifest build/nor-debug/firmware-nor.json \
  --xfel /absolute/path/to/xfel
```

将 `flash` 改为 `verify` 可只校验；`probe` 只需要 `--helper-manifest` 和可选的 `--xfel`。增加 **`--dry-run`** 只校验本地文件、SHA256、头部和 SRAM 布局，不访问 USB，也不会下载、擦写或执行。

除普通 NOR 镜像外，构建新增以下产物：

| 文件                  | 用途                              |
| --------------------- | --------------------------------- |
| `fel_nor_helper.elf`  | 带符号的 SRAM 烧写程序            |
| `fel-nor-helper.bin`  | ARM 入口 + Thumb 实现，约 1.6 KiB |
| `fel-nor-helper.json` | 地址、ABI、长度和 SHA256          |
| `fel-nor-helper.map`  | 烧写程序链接布局                  |

辅助程序从 [C 源码](../boot/nor/fel_nor.c) 和 [入口汇编](../boot/nor/fel_nor_entry.S) 构建，不增加 prebuilt。镜像和清单必须来自同一次构建；不要用普通 `firmware.bin` 代替 `firmware-nor.bin`。

## 传输与 SRAM 布局

主机先核对 R328 FEL ID `0x00182100`、scratchpad `0x27E00`，再下载并回读校验烧写程序。每条命令写入邮箱，调用 `xfel exec 0x28000`，读回结果；程序完成后返回 BROM，所以 USB FEL 持续可用。

| SRAM 地址            | 用途                     |
| -------------------- | ------------------------ |
| `0x28000–0x28FFF`    | 烧写程序，链接上限 4 KiB |
| `0x29000` 起 36 字节 | 命令邮箱                 |
| `0x2A000–0x2DFFF`    | 16 KiB 数据交换区        |
| `0x2E000–0x2EFFF`    | 空隙                     |
| `0x2F000–0x2FFFF`    | 私有栈，栈顶 `0x30000`   |

不会覆盖低端 BROM 工作区或高端 BROM 栈，也不使用 FULL FEL 应用的搬运引导。入口保存并恢复 BROM 的寄存器、SP、LR、CPSR，C 代码使用私有栈；不改变运行模式、DDR、PLL、VBAR 或 GIC。要求 CPU0、MMU / D-cache 关闭，超时基于现有 24 MHz 架构计数器。

邮箱携带序号、操作、地址、长度、CRC32、状态和完成字节数；CRC32 覆盖请求头及待编程的数据。序号 / 参数回显不一致、CRC 错误、未知芯片或超时均停止操作。SPI FIFO 有界轮询，出错时复位 SPI0；SPI1 时钟和复位位保持原样。

擦除逐个执行 `D8h` 64 KiB Block Erase，避免整片擦除长时间阻塞 USB。编程使用 `06h` Write Enable 和 `02h` Page Program，自动拆分 256 字节页，片选在单页多个 FIFO 之间保持有效。每页及每块等待 BUSY 清除。全 `0xFF` 的传输块跳过编程，但仍参与最终整片回读。指令及时序依据 [Winbond W25Q64JV 数据手册](https://media.digikey.com/pdf/Data%20Sheets/Winbond%20PDFs/W25Q64JV_RevK_3-10-21.pdf)。

## 错误与验证范围

- `Expected R328` / scratchpad 不符：尚未下载，检查芯片和 BROM FEL 状态。
- SRAM 回读或 CRC 不符：命令停止；检查 USB 连接后重新进入 FEL。
- JEDEC 不符：先确认进入 FEL 后已松开 FEL 键，再检查型号、SPI0 接线及 `/WP`、`/HOLD` 电平，不会按其他器件继续擦写。
- 写保护 / WEL 被拒绝：程序不会自动修改保护、QE 或状态寄存器。按具体器件配置解除保护后重试。
- USB、SPI 或 BUSY 超时：操作可能已经开始，不自动重试；若正在擦写，应修复问题并重新执行完整 `fel-flash`。
- 校验不符：报告第一个错误的 Flash 地址和字节；包括个别块锁定等未能实际写入的情况。

软件验证包括：实际 C 代码的 JEDEC、CRC、保护、FIFO、片选和页边界模型测试；主机脚本的完整擦写 / 整片回读、错芯片、损坏文件和中途失败测试；QEMU 执行真实入口汇编，验证私有栈和重复调用后寄存器 / CPSR 恢复。

**这些测试不模拟 R328 的真实 BROM USB 服务或 Flash 电气时序，目前尚未完成实板擦写和冷启动验证。** 本次开发过程没有对连接的硬件执行擦除或编程。
