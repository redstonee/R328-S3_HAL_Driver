# SPI NAND 直接启动

启动流程只有两级：

```text
BROM → SRAM 中的 boot0：初始化电源、时钟和 DRAM
                     → 从 SPI NAND 读取应用包、复制应用到 DDR
                     → 关闭 MMU/cache → 0x40000000 → Reset_Handler → main
```

构建仅使用 [prebuilt/r328](../prebuilt/r328/) 中的 **32 KiB boot0** 和 NAND 配置；不需要原系统镜像、SDK、U-Boot、OP-TEE、DTB、Linux 或文件系统。DDR 训练代码仍是 boot0 内的原厂二进制，没有还原为 C 源码。

SPI NOR 的线性烧写布局见 [W25Q64 说明](nor-image.md)。

## 构建

```sh
cmake --preset nand-debug
cmake --build --preset nand-debug
ctest --preset nand-debug
```

体积优化版本使用 `nand-release`。应用仍是 `Core/Src/*.c`，修改后构建会自动重新打包。单独生成启动产物可用 `cmake --build build/nand-debug --target flash-image`。

| 文件（位于 `build/nand-debug/`） | 内容                                                              |
| -------------------------------- | ----------------------------------------------------------------- |
| `firmware-nand.bin`              | 4 MiB 的 SPI NAND **物理主数据区**镜像，含 boot0 和应用的多个副本 |
| `boot0.bin`                      | 填入 NAND、UART 配置并重算 checksum 的 32 KiB boot0               |
| `firmware.toc1`                  | 只有一个裸机应用条目的 TOC1 包，长度按 4 KiB 对齐                 |
| `firmware.elf` / `.bin` / `.map` | 应用符号、二进制与链接布局                                        |
| `firmware-nand.json`             | 页与块布局、各副本物理地址、哈希、DDR 和烧写约定                  |

`firmware-nand.bin` **不是 PhoenixSuit / LiveSuit 的 IMAGEWTY 包，也不是含 OOB/ECC 字节的裸 dump**。不能再使用以前的 `firmware-nand.img` 或 `firmware.uImage` 流程。

## DDR 与应用入口

应用链接窗口为 **128 MiB**，`0x40000000–0x47FFFFFF`，栈位于窗口顶端。没有常驻安全固件，也不再沿用原系统 DTB 的内存保留区。容量来自所提供的硬件信息，尚未实板测量，清单保留 `capacity_measured: false`；实际容量和时钟应查看 boot0 训练日志。

boot0 先将 TOC1 读到 `0x42E00000`，然后将应用复制到 `0x40000000`。复制完成、关闭 MMU 后该暂存区及页表不再使用，可以由应用的 BSS、空闲区使用；应用 BIN 的复制范围不能与暂存源重叠，工具会检查。NAND 启动区域另限制 BIN 最大 **3,145,216 字节**；BSS 不占 BIN。

[Flash 启动头部](../Core/Startup/flash_header.S) 在八个异常向量之后保留元数据，使 BIN 的 `+0x2C` 为加载地址 `0x40000000`，真正的代码从 `+0x200` 之后开始。头部也在 ELF 内，ELF 与 BIN 保持一致；不能直接打包普通 `ddr-debug` 产物。

TOC1 条目名使用 `freertos`，这是该版本 boot0 内已有的**直接应用分发标识**，不表示工程依赖 FreeRTOS。反汇编确认它将应用地址存入独立入口变量，跳过 U-Boot 头修改、monitor/OP-TEE 交接分支。应用维持 Secure SVC，`nand-*` 使用 `SECURE` GIC Group 0，UART0 默认 PH0/PH1、115200 8N1。

## NAND 配置与烧写布局

默认 `prebuilt/r328/spinand.json` 对应 **MX35LF1GE4AB**：128 MiB、2,048 字节主数据 / 64 字节 OOB 每页、64 页每块、1,024 块，ID `C2 12`（`9Fh` 后带一个 dummy 字节）。依据 [Macronix 数据手册](https://www.macronix.com/Lists/Datasheet/Attachments/9075/MX35LF1GE4AB,%203V,%201Gb,%20v2.0.pdf) 和 [Linux Macronix 驱动](https://github.com/torvalds/linux/blob/master/drivers/mtd/nand/spi/macronix.c)。boot0 的通用 SPI NAND 路径从头部读取几何参数，将 B0 配置为 `0x10`（内部 ECC 开、单线），用 `13h` 页读取和 `0Bh` cache 读取；E4AB 支持此组合，不修改 boot0 的 NAND 代码或 DDR 训练。

原 FS35ND04G-S2Y2（512 MiB，ID `CD EC 11`）配置保留在 `spinand-fs35.json`。可用 `cmake --preset nand-debug -DR328_NAND_PROFILE=prebuilt/r328/spinand-fs35.json` 选择；该配置仅支持镜像打包，不提供本次 E4AB 专用烧写目标。不同型号不可混用。

原提取文件的 `storage_data` 为零，不能直接作为可用冷启动 boot0。打包脚本在 `0x1F8` 起填充 96 字节存储参数，并设置启动介质、UART，重新计算 eGON checksum；代码和 DDR 参数保持原样。

| 物理块    | 内容                                                                                  |
| --------- | ------------------------------------------------------------------------------------- |
| 0、2、4、6 | 每块起始放一个 boot0 副本，连续使用 16 个完整的 2 KiB 主数据页 |
| 1、3、5、7 | 保留为 `0xFF` |
| 8–31      | TOC1 应用包副本，按完整主数据页连续存储，副本按块对齐                                 |
| 32 及以后 | 不在镜像中                                                                            |

boot0 配置的包扫描范围为块 8–31（本二进制的末块判断包含上界）。一个包占 `ceil(包长 / 128 KiB)` 个块，区域内尽量放入多个完整副本；包越大，可用副本越少。清单列出每个副本的物理地址。默认小应用有 24 份应用包和 4 份 boot0。

从实板读取的 R328 BROM 确认：SPI NAND 路径使用 2 KiB 页数据，先尝试第 0 页，再尝试第 128、256、…、896 页，因此前 8 个物理块内可用的 boot0 起点是块 0、2、4、6。**不能使用每页只放 1 KiB 的旧镜像**；这种镜像可以烧写并回读一致，但 ROM 加载后 checksum 不匹配，会回到 FEL 且无 boot0 串口日志。修复后必须重新打包并烧写，单独更新烧写脚本不能修复 Flash 中的旧布局。逆向依据见 [冷启动排查](nand-boot-debug.md)。

烧录器必须按**物理块地址**写入，启用芯片内部 ECC，保留出厂坏块标记。遇到坏块时跳过该物理块，后续数据仍对应镜像中原物理偏移，不能将整个镜像紧缩后写入。跨越坏块的副本可能失效，boot0 会继续扫描其他副本；这不保证任意坏块分布都可启动。只写主数据，不用本镜像覆盖 OOB，也不能把这里的 SPI NAND 布局用于并行 NAND。

MX35LF1GE4AB 现可通过原版 xfel 的 `read/write/exec` 和 SRAM 辅助程序烧写，不依赖上游 R328 的 SPI NAND 后端。构建会生成 `fel-nand-helper.elf/.bin/.json`；`fel-probe` 扫描启动区坏块，`fel-flash` 烧写并校验，`fel-verify` 只校验。具体步骤和接线见 [xfel NAND 烧写](fel-nand.md)。**4 MiB 擦写、全量回读、坏块标记保持及断电冷启动均已通过实板验证，应用 tick 正常**；具体镜像哈希与验证范围见 [排查记录](nand-boot-debug.md)。

## 独立打包与校验

脚本只使用 Python 标准库，默认从工程的 `prebuilt/r328/` 读取配置：

```sh
python3 tools/nand_image.py pack \
  --elf build/nand-debug/firmware.elf \
  --firmware build/nand-debug/firmware.bin \
  --output build/nand-debug/firmware-nand.bin \
  --package build/nand-debug/firmware.toc1 \
  --configured-boot0 build/nand-debug/boot0.bin \
  --manifest build/nand-debug/firmware-nand.json
```

将 `pack` 改为 `verify` 会重新构造并逐字节检查四个输出，不改写文件。校验包括 prebuilt SHA256、eGON/TOC1 checksum、加载地址、向量、ELF/BIN 一致性、NAND 容量与页布局。输出不带时间戳，相同输入可复现。

单元测试检查损坏输入、头部、参数修改范围、每一页和所有副本，并按 R328 ROM 的 2 KiB 页读取方式重建 boot0、校验 checksum，确保旧的 1 KiB 页布局被拒绝。安装 `qemu-system-arm` 时，NAND CTest 另运行 **boot0 真实 TOC1 解析、复制和直接跳转代码**：验证整个应用字节不被头部回填破坏，入口为 SVC 且 MMU/D-cache 关闭。测试从 NAND 已读入的位置开始，仅替换仿真中的串口打印和硬件延时；**不模拟 DDR 训练、BROM 读 NAND 或芯片 ECC，不代表实板冷启动已经通过**。

## prebuilt 维护

预编译文件来源、SHA256 和原有许可说明见 [prebuilt README](../prebuilt/r328/README.md)。项目不再保留完整系统镜像、FES1 或原 boot package；普通构建不联网、不提取系统镜像。更换 boot0 / NAND 型号需要核对 ABI、DDR 参数及读取方式后适配脚本，不能仅修改 JSON 绕过已核对的配置。
