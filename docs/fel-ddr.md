# FEL 下载到 DRAM

`fel-ddr-debug` / `fel-ddr-release` 使用 SRAM loader 初始化 DRAM，返回 BROM FEL，再通过 USB 将应用下载到 DDR、回读校验并执行。应用默认链接于 **`0x40000000`，使用 128 MiB DDR 窗口**，不受 SRAM 暂存区 64 KiB 的 BIN 限制。

整个流程只调用 xfel 的 `version`、`write`、`read`、`exec`，不读写 SPI Flash，不需要先烧入 bootloader，适合频繁验证较大的程序、字体、图片和音频数据。

## 构建与运行

```sh
cmake --preset fel-ddr-debug
cmake --build --preset fel-ddr-debug
ctest --preset fel-ddr-debug
# 按住 FEL 键复位，进入 BROM FEL 后立即松开按键，再执行：
cmake --build build/fel-ddr-debug --target fel-run
```

FEL 键仅在进入 FEL 时按住，进入后立即松开；松开不会退出 FEL，下载和 Flash 操作期间都应保持松开。xfel 不在 PATH 时，配置增加 `-DR328_XFEL=/absolute/path/to/xfel`。发布构建使用 `fel-ddr-release`。

所有预设都编译当前 `Core/Src/*.c`。应用自行配置外设和 UART；loader 不配置 UART，也没有串口日志。主机终端应依次显示：

```text
DRAM initialized: 128 MiB. BROM FEL returned; downloading application.
DDR readback PASS: ... bytes. Executing application (FEL will stop).
```

| 文件 | 用途 |
| --- | --- |
| `firmware.bin` / `.elf` / `.map` | DDR 应用、符号和链接布局 |
| `fel-dram-loader.bin` | 从源码构建、可返回 BROM 的 SRAM loader |
| `fel-dram-vendor.bin` | 从已有 boot0 prebuilt 提取的 DDR 初始化代码与参数 |
| `firmware-fel-ddr.json` | 下载地址、文件长度和 SHA256 清单 |

普通构建和 CTest 不访问 USB。也可直接使用脚本：

```sh
python3 tools/fel_dram.py run \
  --manifest build/fel-ddr-debug/firmware-fel-ddr.json --dry-run
python3 tools/fel_dram.py run \
  --manifest build/fel-ddr-debug/firmware-fel-ddr.json --xfel /absolute/path/to/xfel
```

应用接管后 FEL 停止服务。再次下载时重新进入 BROM FEL，无需擦写 Flash。`exec` 应用时 USB 断开或超时，不一定代表启动失败，应查看应用输出；此前任一步骤失败，脚本都会停止，禁止执行应用。

## 加载流程与 SRAM 保护

1. 检查 ELF/BIN 一致性、ARM 向量、地址范围、loader ABI、prebuilt 指纹及文件 SHA256，保存本次下载的数据快照。
2. 检查 R328 BROM ID `0x00182100`、scratchpad `0x27e00`，下载 vendor 代码和 loader，逐项回读校验。
3. 执行 loader，屏蔽 IRQ/FIQ，保存 ROM 寄存器、CPSR 和 SP，换用私有栈。
4. 备份低端 SRAM `0x20000–0x27FFF`，将 vendor 代码复制到其原链接地址，清零其 BSS，同步指令缓存，调用 Thumb DDR 初始化入口 `0x22DAD`，参数为 `r0=0`、`r1=0x20038`。
5. 保存容量返回值，恢复完整低端 SRAM、指令缓存一致性和 ROM 调用现场，返回 FEL。初始化返回失败时也执行恢复。
6. 主机检查初始化结果和容量，再次查询 FEL 版本；以最多 256 KiB 一块写入 DDR，每块回读逐字节比较。
7. 全部比较通过后才执行应用入口。应用启动代码接管向量、异常栈和 BSS。

| SRAM 范围 | 用途 |
| --- | --- |
| `0x20000–0x27FFF` | ROM 工作区；临时放置 vendor 代码，返回前完整恢复 |
| `0x28000–0x2B0FF` | vendor 代码暂存，`0x3100` 字节 |
| `0x2C000–0x2CEFF` | loader 代码窗口 |
| `0x2CF00–0x2CF0F` | 初始化结果：magic、ABI、状态、容量 MiB |
| `0x2D000–0x34FFF` | 低端 SRAM 的 32 KiB 备份 |
| `0x35000–0x37FFF` | loader 私有栈，含底部保护字 |
| `0x38000–0x3FFFF` | 保留给 ROM，包括实机观测到的 `0x3C5DC` 调用栈 |

临时存储位于已有 SRAM 下载流程验证过的 `0x28000–0x37FFF` 窗口。loader 检查 CPU0、MMU / D-cache 关闭，以及 ROM 栈未落入临时区域；结束时检查私有栈保护字。不执行 boot0 的启动入口、Flash 读取器、MMU 初始化或应用复制路径。

## DDR 参数和验证范围

复用 [必要的 boot0 prebuilt](../prebuilt/r328/README.md) 中用于 NAND 冷启动的 DDR 参数，不增加新的预编译文件。打包严格检查完整 boot0 SHA256，提取前 `0x3100` 字节，包含参数、训练、芯片检查及算术 / 延时辅助代码。vendor `printf` 入口替换为返回 0 的 Thumb 桩，避免依赖 UART 和未提取的日志字符串。原始 prebuilt 不修改。

此路径针对当前 R328-S3 和既有 DDR 参数，不是通用的开源 DRAM 训练驱动。128 MiB 是配置容量，脚本同时检查初始化函数报告的容量；应用 BIN、BSS、静态数据和异常栈必须装入链接窗口。

主机测试覆盖超过 64 KiB 的应用、多块写入及回读、文件变化、错误芯片、初始化失败、容量不符和超时。QEMU 执行真实 SRAM loader，检查完整低端 SRAM 恢复、BSS、私有栈、寄存器 / CPSR 和错误返回；vendor DRAM/MMIO 由测试桩替代，因此不能证明实机 DRAM 训练和 USB 返回成功。

2026-11-26 实板验证通过：下载并回读 12,544 字节 vendor 代码和 448 字节 loader；初始化返回容量为 128 MiB，随后 BROM FEL 版本查询正常。当前应用的 5,068 字节已下载到 `0x40000000`、完整回读一致并执行，用户确认串口输出与 tick 持续正常、两个普通 LED 正常闪烁。全程未操作 Flash。此记录验证了初始化、返回 FEL 和 DDR 应用运行；超过 64 KiB 的多块下载目前由主机测试覆盖，尚未实板验证大 BIN 或完整 DDR 空闲区。

需要测试空闲 DDR 时，按 [DRAM 例程](../example/dram/README.md) 复制源码后使用本预设。若 loader `exec` 超时，重新进入 FEL 后重试，保留终端日志用于排查。
