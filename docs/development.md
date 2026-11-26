# 开发与代码格式

## 格式约定

| 文件 | 工具 / 规则 |
| --- | --- |
| C 源码和头文件 | clang-format，`.clang-format`：4 空格、Allman 大括号、100 列、不重排 include |
| Python | Ruff，`ruff.toml`：4 空格、双引号、100 列 |
| CMake | cmake-format，`.cmake-format.json`：4 空格、100 列 |
| JSON | 2 空格缩进，文件末尾换行 |
| 汇编、链接脚本 | 手工保持 4 空格缩进，标签和块结构分行 |
| Markdown | 按用途组织；命令、配置和限制放在对应主题文档 |

构建目录由 CMake 生成，不直接格式化或修改。链接脚本应修改 `linker/r328_ram.ld.in`，再重新配置。

可在独立 Python 环境中安装本次使用的格式工具；固件构建不依赖这些工具：

```sh
python3 -m venv build/format-venv
. build/format-venv/bin/activate
python3 -m pip install -r tools/requirements-format.txt
```

从工程根目录格式化，下面的文件枚举使用 ripgrep：

```sh
rg --files -0 -g '*.c' -g '*.h' Core Drivers boot example tests | xargs -0 clang-format -i
ruff format tools tests
cmake-format -i CMakeLists.txt tests/CMakeLists.txt cmake/arm-none-eabi.cmake
```

检查格式而不改文件：

```sh
rg --files -0 -g '*.c' -g '*.h' Core Drivers boot example tests | xargs -0 clang-format --dry-run --Werror
ruff format --check tools tests
cmake-format --check CMakeLists.txt tests/CMakeLists.txt cmake/arm-none-eabi.cmake
```

## 验证修改

主机模型测试：

```sh
cmake -S tests -B build/host-tests -G Ninja
cmake --build build/host-tests
ctest --test-dir build/host-tests --output-on-failure
```

交叉构建与布局检查：

```sh
cmake --preset fel-debug
cmake --build --preset fel-debug
ctest --preset fel-debug
```

验证例程时，按 [复制说明](../example/README.md) 将源码和头文件放入 `Core/Src/`、`Core/Inc/`，继续使用同一内存 / 加载预设。启动、链接或优化相关修改还应检查 `fel-release` 以及 NAND / NOR 的 DDR 构建配置。

验证 NAND 打包时，运行 `nand-debug` / `nand-release` 的构建与 CTest，不需要系统镜像。校验会从 prebuilt 和应用重建页布局、各副本和校验值；安装 QEMU 时另执行 boot0 的真实复制和直接交接路径。DDR / NAND 硬件初始化仍需实板验证，见 [NAND 说明](nand-image.md)。

验证 NOR 时运行 `nor-debug` / `nor-release` 的构建与 CTest。主机测试覆盖实际 NOR 读取源码的寄存器 / FIFO 协议、超时及 8 MiB 镜像边界；QEMU 验证补丁分发、复制和交接，读取器入口由成功返回桩替代。BROM、DDR 训练和 SPI 时序仍需实板验证，见 [W25Q64 说明](nor-image.md)。

FEL NOR 烧写新增 C 寄存器 / FIFO 模型测试和主机传输测试；NOR CTest 还检查辅助程序布局，并在可用时用 QEMU 验证真实入口的寄存器、SP、LR 和 CPSR 恢复。见 [xfel NOR 烧写](fel-nor.md)。

CTest 不下载程序或访问 USB。设备访问仅由显式执行的 `fel-run`、`fel-probe`、`fel-verify`、`fel-flash` 目标触发；其中 `fel-flash` 擦写整颗 NOR。主机模型不验证实际电气、时钟频率或设备运行效果。

FEL DDR 使用 `fel-ddr-debug` / `fel-ddr-release`，CTest 检查清单、DDR 链接布局及真实 ARM loader 的 ROM SRAM / 调用现场恢复；主机 `fel_ddr_host` 测试大于 SRAM 暂存上限的多块下载和失败停止。vendor DRAM 训练在 QEMU 中以桩替代；实板已确认初始化返回 128 MiB、返回 FEL 和当前应用运行正常，大 BIN 多块传输尚未实板验证。`fel-run` 只下载到 RAM，不读写 Flash，详见 [FEL DRAM](fel-ddr.md)。

FEL NAND 使用同一 SPI FIFO 传输层和主机 FEL 协议。`fel_nand_protocol` 检查 E4AB 的 ID dummy、两页坏块标记、内部 ECC、整页编程、块边界、保护恢复及错误状态；`fel_nand_host` 检查坏块物理偏移、完整副本预检查、擦写范围、回读和失败停止。NAND CTest 增加辅助程序布局及真实汇编入口返回测试。测试不接触 USB，实板步骤见 [xfel NAND 烧写](fel-nand.md)。
