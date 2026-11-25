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
rg --files -0 -g '*.c' -g '*.h' Core BSP Drivers example tests | xargs -0 clang-format -i
ruff format tools tests
cmake-format -i CMakeLists.txt tests/CMakeLists.txt cmake/arm-none-eabi.cmake
```

检查格式而不改文件：

```sh
rg --files -0 -g '*.c' -g '*.h' Core BSP Drivers example tests | xargs -0 clang-format --dry-run --Werror
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

修改独立例程时，替换为对应预设，例如 `fel-spi-ili9341`。启动、链接或优化相关修改还应检查 `fel-release`、`sram-debug` 和 `ddr-debug`。

CTest 不下载程序或访问 USB。硬件下载仅由显式执行的 `fel-run` 目标触发。主机模型不验证实际电气、时钟频率或设备运行效果。
