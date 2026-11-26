# 通过 xfel 烧写 MX35LF1GE4AB

`tools/fel_nand.py` 使用原版 xfel 的 `version/write/read/exec`，将从源码构建的辅助程序放到 SRAM 执行。每条命令完成后返回 BROM，再传输下一块数据；不调用 xfel 的 SPI NAND 后端，也不初始化 DDR。默认 NAND 配置已经切换到 **MX35LF1GE4AB**，不兼容 E4AD、24AD 或其他 ID 的芯片。

## 接线与器件参数

| 信号 | R328 引脚 | MX35LF1GE4AB |
| --- | --- | --- |
| SCLK | PC0 / AF4 | SCLK |
| MOSI | PC2 / AF4 | SI / SIO0 |
| CS0 | PC3 / AF4 | CS# |
| MISO | PC4 / AF4 | SO / SIO1 |
| 电源 / 地 | 3.3 V / GND | VCC / VSS |
| WP#、HOLD# | 板上拉高 | WP# / SIO2、HOLD# / SIO3 |

使用 SPI0 / CS0、Mode 0、单线、12 MHz；boot0 日志默认 PH0 / PH1、115200 8N1，应用自行配置 UART。WP#、HOLD# 不应悬空。芯片容量 **128 MiB**，每页 2,048+64 字节，每块 64 页 / 128 KiB，共 1,024 块，`9Fh + dummy` 读出的 ID 为 `C2 12`。参数和指令依据 [Macronix E4AB 数据手册 Rev. 2.0](https://www.macronix.com/Lists/Datasheet/Attachments/9075/MX35LF1GE4AB,%203V,%201Gb,%20v2.0.pdf)。

## 构建与烧写

安装 `arm-none-eabi-gcc`、CMake、Ninja、Python 3 和支持 R328 FEL 的 xfel。Python 脚本只用标准库。xfel 不在 PATH 时，在配置时指定绝对路径：

```sh
cmake --preset nand-debug -DR328_XFEL=/path/to/xfel
cmake --build --preset nand-debug
ctest --preset nand-debug
```

如果 xfel 已在 PATH 中，第一条命令直接使用 `cmake --preset nand-debug`。也可使用 `nand-release` 及相应构建目录。

按住 FEL 键复位，让芯片进入 **BROM FEL**，然后**立即松开 FEL 键**。松开后仍保持 FEL 状态。探测、读取、校验及烧写期间都必须保持按键松开，持续按住会导致 SPI Flash 无法读写。先探测芯片及前 32 块的坏块标记：

```sh
cmake --build build/nand-debug --target fel-probe
```

确认输出为 `MX35LF1GE4AB`、`JEDEC 00C212`。随后执行烧写：

```sh
cmake --build build/nand-debug --target fel-flash
```

**该命令会覆盖前 4 MiB 启动区域中的好块。** 块 32–1023（后 124 MiB）不擦写。脚本扫描坏块、检查可用完整副本、擦除好块、按页编程并回读所有好块的主数据，包括镜像中的 `FF` 空白区域，最后再次检查坏块标记。成功显示 `NAND readback PASS`。保持 FEL 键松开，复位或断电重启，即可尝试冷启动。

单独回读校验，不擦写 Flash：

```sh
cmake --build build/nand-debug --target fel-verify
```

普通构建和 CTest 不访问 USB；只有显式运行上述三个 `fel-*` 目标才会操作设备。**4 MiB 擦写、完整回读、坏块标记保持及断电冷启动已通过实板验证，应用 tick 正常**。旧版每页 1 KiB 的 boot0 布局无法通过 R328 ROM 校验；请重新构建生成每页 2 KiB 的镜像再烧写。具体镜像哈希与验证范围见 [排查记录](nand-boot-debug.md)。

## 独立脚本与离线检查

构建产物包括 `firmware-nand.bin/.json`、`fel-nand-helper.elf/.bin/.json`。辅助程序不作为额外 prebuilt 保留，每次从工程源码构建。

```sh
python3 tools/fel_nand.py flash \
  --helper-manifest build/nand-debug/fel-nand-helper.json \
  --image build/nand-debug/firmware-nand.bin \
  --manifest build/nand-debug/firmware-nand.json \
  --xfel /path/to/xfel
```

将 `flash` 改为 `verify` 可只校验；`probe` 只需 `--helper-manifest` 和可选 `--xfel`。加 `--dry-run` 会检查输入文件、哈希、辅助程序 ABI 和整个 NAND 页布局，完全不访问 USB。脚本只接受本工程生成的 E4AB 物理主数据镜像，不能传入旧 FS35 镜像、带 OOB 的 dump 或原系统镜像。

## 坏块、ECC 与保护

每次烧写前读取物理块 0–31 的**第一页和第二页**，检查 OOB 第 0 字节；任一个字节非 `FF` 即跳过整个块。该标记位于芯片 ECC 不覆盖的 M2 区域，所以检查标记时不因主数据 ECC 错误而误判，也不需要关闭内部 ECC。

跳过坏块时保留镜像原物理偏移，后续块不前移。块 0–7 保存 boot0 副本，块 8–31 保存应用包副本。跨坏块的应用副本不计为可用副本；擦除前必须至少保留一个好块中的 boot0 和一个完全落在好块上的应用包，否则停止。镜像布局见 [NAND 启动说明](nand-image.md)。

写入使用 `02h` 加载整页主数据，然后 `10h` 一次执行，不做局部页反复编程。`02h` 将未加载的 cache 字节清为 `FF`，包括 spare metadata；内部 ECC 将校验码保存在隐藏 spare 中。脚本不上传 OOB、不写坏块标记。读取主数据时接受已纠正的 1–4 bit 错误；不可纠正或保留的 ECC 状态会报错并停止。每次擦除和编程均检查 OIP、WEL、E_FAIL / P_FAIL。

E4AB 上电默认整片写保护。执行擦写时，辅助程序临时清除易失的 A0 块保护位，操作结束后恢复先前值；不绕过 BPRWD / Solid Protection，不访问 OTP 或设置永久保护位。B0 的内部 ECC 会保持启用；若芯片已经进入 OTP 访问模式则直接拒绝操作。

遇到 USB 中断、BUSY 超时、擦写失败或回读不一致会立即停止，不自动重试、不搬移副本、不自动将块标为坏块。中断时启动区可能只写入了一部分，日志会报告失败的物理地址。先确认 FEL 键已经松开，再检查连接等原因；必要时重新进入 FEL、立即松开按键后执行完整烧写。

## SRAM 与测试范围

辅助程序入口 `0x28000`，代码限于 4 KiB；邮箱 `0x29000`，16 KiB 传输缓冲 `0x2A000`，私有栈 `0x2F000–0x2FFFF`。保留 BROM 的 `0x27E00` scratchpad 和高端栈，不使用完整 SRAM 应用的重定位入口。

主机先检查 R328 ID `0x00182100` 和 scratchpad，再下载并回读辅助程序。邮箱请求及编程数据带 CRC32，响应检查序号、命令回显、状态和完成长度。辅助程序再次检查 E4AB ID、坏块、页 / 块对齐和范围；硬件端也禁止擦写前 4 MiB 之外的地址。

C 模型测试执行真实辅助程序的 SPI FIFO 代码，覆盖长帧 CS、ID dummy、两个标记页、ECC、保护恢复、整页编程和错误路径；Python 模型验证坏块物理位置、完整副本检查、擦写范围及失败停止；QEMU 验证真实汇编入口恢复寄存器、LR、SP 和 CPSR 并重复返回。它们不模拟真实 NAND 磨损、USB、DDR 训练或 BROM 冷启动读取。
