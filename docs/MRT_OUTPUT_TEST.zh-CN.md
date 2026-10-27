# MRT 未声明输出测试

测试代码：[MrtOutputTest.cpp](../avboit/tests/MrtOutputTest.cpp)、
[MrtOutputTest.hlsl](../avboit/shaders/MrtOutputTest.hlsl)。仅在 `BUILD_TESTING=ON`
时构建，不改变 viewer 的渲染管线。

## 测试方法

- 原生 DX12 PSO，使用项目现有 DXC 编译 DXIL。Donut 仅负责设备初始化和加载 shader；
  不经过 NVRHI 的 PSO / framebuffer 创建，不让框架自动关闭未声明输出的 write mask。
- 2560×1440、四张独立 RT，分别测试 R11G11B10_FLOAT 和 RGBA16_FLOAT。
  所有 RT 先通过 draw 写入不同的非零空间图案，避免依赖 fast-clear 常量。
- 对照只绑定一张、绑定四张但仅声明 Target0、显式关闭其余 write mask、
  声明全部但关闭 write mask、三个输出、四个输出、输出零和 Target0/3 的空槽。
  另外比较三张绑定三个输出与四张绑定三个输出。
- 分别使用覆盖写和 ONE/ONE 加法混合；读取全部 RT，逐像素比较原始存储位。
  未声明输出测试的所有已绑定槽位均为 `WriteMask=ALL`。
  正常写入的槽位必须明显改变，避免一次未执行的 draw 被误判为通过。
- 性能测试强制 StablePowerState，关闭 debug layer。每种格式/混合方式先预热 8 轮，
  每轮随机排列模式，每个模式连续绘制 32 次后将 GPU timestamp 耗时除以 draw 数。
  初始化、纹理读回、CPU 等待不在计时区间内；RT 绑定和 PSO 切换计入。
  各输出共享颜色 hash，避免四输出额外计算四遍 hash。

默认使用与 viewer 相同的 `OMSetRenderTargets` 绑定方式。可用 `--render-pass`
测试真正的 `BeginRenderPass`，要求 runtime 的 `D3D12_OPTIONS18.RenderPassesValid`
成立；不支持时明确失败，不静默更换绑定路径。

## 运行

配置阶段启用 `BUILD_TESTING=ON` 后：

```powershell
cmake --build build --config Release --target avboit_mrt_test
build/tests/bin/avboit_mrt_test.exe --debug
build/tests/bin/avboit_mrt_test.exe --benchmark --rounds 120 --csv build/mrt-output.csv
```

CTest 的 `mrt_output_gpu` 只执行正确性验证，不要求 StablePowerState / Developer Mode。
性能模式在未启用 Developer Mode 时拒绝输出计时，避免把非稳定频率当作稳定频率结果。

## 如何理解结果

读回可以验证“不修改目标内容”，timestamp 可以比较性能，**二者都不是 DRAM 字节计数器**。
不能根据耗时相同就声称所有 GPU 上额外显存事务严格为零。
清除、附件生命周期、压缩元数据、缓存和 tile store 的成本也不等于 shader 的逐像素颜色输出成本。

这里测试的是编译后输出签名里完全不存在某个 `SV_Target`，不测试“声明了但某个动态分支未赋值”。
后者不能用来实现逐像素跳过写入。参见
[Direct3D PS Output Writes §16.9.1](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
和 [D3D12 Render Pass 规范](https://microsoft.github.io/DirectX-Specs/d3d/RenderPasses.html)。

## 本机结果（2026-10-17）

RTX 3070，驱动 617.14，2560×1440，StablePowerState 开启。
系统 D3D12 runtime 为 10.0.22000.2360，不支持 OPTIONS18 查询，因此此次实际测量使用
`OMSetRenderTargets`；没有将结果标成 `BeginRenderPass` 实测。

两种格式 × 两种 blend × 十种输出/绑定配置，共 40 个正确性用例全部通过。
所有未声明槽位的像素逐位保持原值；正常输出槽位也通过参考值和实际修改数量检查。
单独的 debug-layer 验证为 0 errors / 0 warnings。

以下为加法混合的每次全屏 draw 耗时（ms），取两次独立运行各 120 轮中位数的平均值：

| 配置 | R11G11B10F | RGBA16F |
| --- | ---: | ---: |
| 绑定 1 张，只输出 Target0 | 0.0809 | 0.1655 |
| 绑定 4 张，只声明 Target0，所有 mask 全开 | 0.0799 | 0.1624 |
| 绑定 4 张，只声明 Target0，其余 mask 关闭 | 0.0820 | 0.1670 |
| 声明 4 个输出，但仅 Target0 的 mask 开启 | 0.0815 | 0.1672 |
| 绑定 3 张，输出 3 张 | 0.2273 | 0.5003 |
| 绑定 4 张，只声明前 3 个输出 | 0.2274 | 0.5000 |
| 绑定 4 张，前 3 个正常输出、第四个输出零 | 0.2447 | 0.4890 |
| 绑定 4 张，4 个正常输出 | 0.3176 | 0.6677 |
| 绑定 4 张，Target0 正常输出、其余输出零 | 0.2375 | 0.2463 |
| 绑定 4 张，只声明 Target0 和 Target3 | 0.1446 | 0.3344 |

多绑但不声明输出，与少绑 RT 的对照处于同一性能水平，没有出现多张 RT 正常输出的成本。
输出零与不声明输出并不等价：即使加法混合结果相同，前者在部分配置上仍有明显额外耗时。
第四张输出零在 RGBA16F 的三输出对照里没有测出额外成本，不应据此假设所有零输出都昂贵或免费。

桌面环境存在计时波动，两次运行的绝对中位数有约 7%～15% 的变化；
不要将表格里相邻配置的几个百分点解释成稳定的优化收益。
这些是固定分辨率、简单 shader、重复全屏绘制的微基准，不是 AVBOIT 整帧耗时，也不是跨 GPU 的带宽保证。

## Write mask 会不会裁掉 PS 计算（2026-10-19）

新增 `--shader-work`，仍使用同一个原生 DX12 测试程序，不修改 viewer。
Target0 始终输出便宜的颜色，Target1 分别输出 64/256 轮依赖 ALU 运算，或
8/32/128 次依赖纹理采样的结果。纹理为固定种子的 512×512 RGBA8 随机图，
线性 Wrap 采样；下一次地址依赖上一次采样结果，防止合并重复读取。

每种重计算 shader 都使用**完全相同的 PS 字节码**创建两个 PSO：
Target1 write mask 为 0 或 ALL。另一个精简 PS 只声明 Target0，作为计算已删除的参考。
四张 RT 始终绑定，Target2/3 的 mask 关闭。覆盖写和加法混合均测试。
重计算没有 UAV、discard、深度等副作用，也不影响 Target0；属于理论上可消除的独立计算。
优化后的 DXIL 仍保留这些循环，纹理版本仍包含采样指令。

```powershell
build/tests/bin/avboit_mrt_test.exe --shader-work --debug
build/tests/bin/avboit_mrt_test.exe --shader-work --benchmark --rounds 48 --draws 4 --csv build/write-mask-work.csv
```

RTX 3070、2560×1440、StablePowerState、原生 OMSetRenderTargets。
两次独立运行：第一次 48 轮、每组 4 draw；第二次 16 轮、每组 2 draw。
每种格式/混合方式另有 8 轮预热，模式顺序随机，debug layer 不参与性能计时。
两次运行结论一致。下面是第二次 R11G11B10F、覆盖写的每次全屏 draw 中位数，单位 ms：

| Target1 专属计算 | mask=0 | mask=ALL |
| --- | ---: | ---: |
| 精简 PS：完全删除 Target1 及其计算 | 0.030 | — |
| 64 轮 ALU | 2.041 | 2.070 |
| 256 轮 ALU | 7.739 | 7.943 |
| 8 次纹理采样 | 1.321 | 1.277 |
| 32 次纹理采样 | 4.983 | 5.041 |
| 128 次纹理采样 | 21.084 | 21.067 |

**本机这些 PSO 的 write mask=0 没有消除相应的重计算。**
masked 耗时随循环/采样数显著增长，与打开写入时接近，远高于精简 PS。
RGBA16F 和加法混合也得到相同趋势。不能把两列几个百分点的差异当作稳定收益，
桌面 GPU 调度仍会引入波动。

44 个正确性用例通过，单独 debug 验证为 0 errors / 0 warnings。
被屏蔽 RT 逐像素逐位保持原值，Target0 正常更新，打开 Target1 后也确实写入。
CTest `mrt_write_mask_gpu` 保留正确性检查，性能不设自动通过阈值。

这些极重的循环刻意放大计算差异，不代表项目材质/pass 的实际耗时。
结论基于耗时缩放和读回，没有读取 GPU 原生 ISA 或 DRAM 计数器，不能据此保证
其他驱动/GPU 不会做此类优化。工程上若需要确定地删去计算，应使用编译期 shader 变体；
不能仅依赖 PSO 的 write mask。
