# DRAM

`memory/` 对已经初始化的 DDR 做一次破坏性读写测试。测试范围为链接符号 `__static_end__` 向上对齐 64 字节，到 `__stack_bottom__`，不包含代码、静态变量、`.noinit` 和所有异常栈。128 MiB DDR 配置下，测试范围接近整个空闲 DDR；启动后已释放的 TOC 暂存区和 boot0 页表也会覆盖。

测试依次执行：

1. 在首个测试字进行 32 位 walking 1 / walking 0 数据线检查。
2. 对整个空闲区写入并回读 `00000000`、`FFFFFFFF`、`55555555`、`AAAAAAAA`。
3. 对整个空闲区写入并回读字地址、反相字地址，以检查地址别名。

每种模式都先写完整个区域再回读，避免分块测试遗漏跨块别名。通过 `volatile` 访问和 DSB 保证访问顺序；要求 MMU、D-cache 关闭，沿用 NAND/NOR boot0 交接后的状态。发现错误时报告首个地址、期望值和实际值并停止测试；全部通过时输出 `DRAM PASS`、耗时及每秒递增的 tick。

## 复制与运行

先把自己的 `Core/Src/main.c` 备份到 `Core/Src/` 以外，按 [例程索引](../README.md) 清理之前例程的附加文件。从工程根目录执行；使用 FEL DRAM loader，无需烧写 Flash：

```sh
cp example/dram/memory/main.c Core/Src/main.c
cmake --preset fel-ddr-debug
cmake --build --preset fel-ddr-debug
ctest --preset fel-ddr-debug
# 按住 FEL 键复位，进入后立即松开按键，再执行：
cmake --build build/fel-ddr-debug --target fel-run
# 观察 PH0/PH1、115200 8N1 串口。
```

加载流程见 [FEL 下载到 DRAM](../../docs/fel-ddr.md)。也可使用 `fel-ddr-release`，或使用 `nand-debug` / `nand-release`、`nor-debug` / `nor-release` 打包冷启动镜像；Flash 烧写步骤见 [NAND](../../docs/fel-nand.md) 和 [NOR](../../docs/fel-nor.md)。外部 loader 需先初始化 DRAM，再通过[自定义构建](../../README.md#构建配置)选择 DDR。`fel-debug` 只加载到 SRAM，本例会报告 `DRAM SKIP`。

测试前不要启动 DMA 或其他会使用空闲 DDR 的任务。本例使用配置的 128 MiB 地址窗口，不自动探测容量；若实际容量或地址译码不符，访问可能异常，别名还可能覆盖正在执行的程序。若要在未知容量下探测或覆盖包括代码、栈在内的整个 DDR，应使用运行于 SRAM 的初始化及测试程序。这里的 PASS 只覆盖打印出的地址范围和这些测试模式，不代替长时间压力、保持时间或温度测试。

例程已通过 `nand-debug` / `nand-release` 交叉构建及镜像、交接和链接布局检查，也通过 `fel-debug` 构建检查；尚未实板运行此 DRAM 测试。验证在独立临时工程中完成，工程当前的 `Core` 应用可继续保留，按上面的复制命令切换即可。
