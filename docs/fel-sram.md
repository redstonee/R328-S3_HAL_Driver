# FEL 下载到 SRAM

`fel-debug` / `fel-release` 默认将 **128 KiB SRAM A1（`0x20000–0x3FFFF`）全部交给应用**，无需初始化 DDR。运行入口为 `0x20000`，栈顶为 `0x40000`。UART0 默认为 **PH0 TX / PH1 RX，AF3，115200 8N1**。

## 构建与运行

```sh
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
# 进入复位后的 BROM FEL：
cmake --build build/fel-debug --target fel-run
```

需要 [xfel](https://github.com/xboot/xfel)。不在 PATH 时，在配置命令后增加 `-DR328_XFEL=/absolute/path/to/xfel`。发布构建使用 `fel-release`，独立例程见 [例程目录](../example/README.md)。

| 文件 | 用途 |
| --- | --- |
| `firmware-fel.bin` | 链接于 `0x20000` 的应用，先暂存在 `0x28000` |
| `firmware-fel-loader.bin` | 下载到 `0x27e00` scratchpad 的两段式引导代码 |
| `firmware-fel.json` | 运行地址、暂存地址、引导地址、长度与 SHA256 |
| `firmware.elf` / `.map` | 符号调试和链接布局 |
| `fel-relocate.bin` | 构建中间产物，打包时填入应用长度 |

产物位于 `build/<预设名>/`。普通构建和 CTest 不访问 USB。`firmware-fel.bin` 必须配合清单和引导代码使用，不能直接在暂存地址执行。

也可直接使用脚本：

```sh
python3 tools/fel_load.py run \
  --manifest build/fel-debug/firmware-fel.json --xfel /absolute/path/to/xfel
# 仅检查文件，不访问设备：
python3 tools/fel_load.py run \
  --manifest build/fel-debug/firmware-fel.json --dry-run
```

程序接管 CPU 后不返回 BROM，USB FEL 停止服务。再次下载需要重新进入 BROM FEL。`exec` 后 USB 报错时，应查看串口或调试器确认应用状态；在 `write` / `read` 阶段失败时，脚本不会执行应用。

## 为什么需要搬运

BROM 同时使用低端和高端 SRAM，不能在 USB 下载期间覆盖它的工作区。实机读取到的 BROM 调用栈指针为 `0x3c5dc`；仅检查 `0x27e00` scratchpad 不能代表全部 BROM 占用。

默认 `R328_FEL_SRAM_PROFILE=FULL` 的流程：

1. 检查本地文件、向量、长度、SHA256，以及 R328 BROM ID `0x00182100` 和 scratchpad 地址。
2. 将应用下载到 `0x28000–0x37FFF` 内，回读并逐字节比较。
3. 将引导代码下载到 BROM 提供的 `0x27e00` scratchpad，回读并比较。
4. 执行 `0x27e00`，屏蔽 IRQ/FIQ 并选择 SVC；从此不返回 BROM。
5. 引导代码将第二段代码搬至 `0x3ff00`，跳转后再将应用从 `0x28000` 正向搬至 `0x20000`。正向复制允许源、目的区域重叠。
6. 同步内存并失效 I-cache / 分支预测后，跳入应用。启动代码接管异常向量、栈、BSS 和 GIC。

两段引导代码均不使用栈。**高端 SRAM 只在放弃 BROM 后由 CPU 改写**，不再通过 FEL 向该区域下载引导代码。应用启动后，可覆盖原 scratchpad、暂存区及引导代码。

## 容量和限制

| 项目 | FULL 默认配置 | DIRECT 回退配置 |
| --- | --- | --- |
| 运行范围 | `0x20000–0x3FFFF`，128 KiB | `0x28000–0x2FFFF`，32 KiB |
| 运行入口 | `0x20000` | `0x28000` |
| USB 暂存地址 | `0x28000` | `0x28000` |
| 单个应用 BIN 上限 | 64 KiB | 20,992 字节 |
| 执行方式 | scratchpad 引导并搬运 | 原地址直接执行 |

128 KiB 是代码、静态数据、BSS、空闲区和异常栈的**运行总容量**。FULL 的 USB 暂存窗口为 64 KiB，所以上限约束的是有实际文件内容的代码和初始化数据；BSS / `.noinit` 不占用 BIN，可使用剩余运行内存。链接器为异常栈预留 11,776 字节，并检查静态数据不与栈重叠。

`0x05480000` 的 VAD SRAM 映射对应同一物理存储，不是额外的 128 KiB。应用依赖 BROM 已准备 SRAM 映射和时钟，不复位 VAD SRAM，不改写 DDR、LDO 或整套 PLL。要求 CPU0、Secure 状态以及 MMU / D-cache 关闭；启动代码保留这些运行限制。

回退到原先的 32 KiB 布局：

```sh
cmake --preset fel-direct
cmake --build --preset fel-direct
ctest --preset fel-direct
# 重新进入 BROM FEL 后：
cmake --build build/fel-direct --target fel-run
```

其他例程也可在配置时增加 `-DR328_FEL_SRAM_PROFILE=DIRECT`。FULL 和 DIRECT 的 ELF、BIN、清单不可混用，切换配置后应重新构建。

## SRAM 测试

```sh
cmake --preset fel-sram-memory
cmake --build --preset fel-sram-memory
ctest --preset fel-sram-memory
# 进入 BROM FEL 后：
cmake --build build/fel-sram-memory --target fel-run
```

[测试程序](../example/sram/memory/main.c) 对 `__static_end__` 到 `__stack_bottom__` 的全部空闲内存执行地址相关模式和反相模式两轮写入 / 回读，打印范围、字节数及 `SRAM PASS`，随后每秒输出 tick。遇到错误时打印首个失败地址和期望 / 实际值。

本次实机验证完成了完整 128 KiB 地址范围的 FEL 读取、64 KiB 暂存镜像及引导代码的下载 / 回读；用户确认应用输出 `SRAM PASS` 且 tick 正常。测试镜像额外填充到 64 KiB，用于验证下载窗口和重叠搬运边界。应用内测试覆盖约 109 KiB 空闲区，不会破坏性覆盖正在执行的代码、静态数据或异常栈，因此不等于逐单元测试整个物理 SRAM。

主机测试检查清单、地址边界、文件变化、下载顺序和回读失败时禁止执行；若安装了 `qemu-system-arm`，FULL 配置的 CTest 还运行真实 ARM 引导代码，验证小镜像、重叠复制及 64 KiB 边界。QEMU 测试不模拟 R328 的外设或 BROM。

## 排查

| 现象 | 检查点 |
| --- | --- |
| 版本查询超时 | 是否处于 BROM FEL、USB 连接与设备访问权限；重新进入 FEL 后重试 |
| 下载或回读失败 | 产物是否来自同一构建、暂存地址与 scratchpad 是否匹配 |
| 执行后串口无输出 | UART 路由、115200 8N1、入口条件、计数器权限和异常记录 |
| BIN 大小检查失败 | FULL 的文件内容上限为 64 KiB；大缓冲区可放 BSS，但仍须满足运行容量 |
| 链接失败 | 静态数据和预留异常栈是否超过运行窗口 |

系统时基使用 24 MHz CNTPCT，见 [时基说明](timebase.md)。修改下载地址不能代替重新链接。
