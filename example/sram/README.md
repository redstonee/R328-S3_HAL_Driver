# SRAM

`memory/` 测试代码、静态数据与异常栈以外的全部空闲 SRAM：先整体写入与地址相关的模式并回读，再反相写入、回读，以检查数据错误和地址别名。成功后输出 `SRAM PASS` 和每秒递增的 tick；失败时输出首个错误地址、期望值与实际值。

```sh
cmake --preset fel-sram-memory
cmake --build --preset fel-sram-memory
ctest --preset fel-sram-memory
# 进入 BROM FEL 后：
cmake --build build/fel-sram-memory --target fel-run
```

默认运行范围为 `0x20000–0x3FFFF`（128 KiB），测试区间为链接符号 `__static_end__` 到 `__stack_bottom__`，当前 Debug 构建约 109 KiB。代码、静态数据和栈仍占用这 128 KiB，不参与破坏性测试。

FULL 下载通过 scratchpad 引导搬运，应用 BIN 暂存上限为 64 KiB，BSS 不占用 BIN。布局、回退配置和实机验证范围见 [FEL 说明](../../docs/fel-sram.md)。本例不初始化 DDR、不写 Flash。
