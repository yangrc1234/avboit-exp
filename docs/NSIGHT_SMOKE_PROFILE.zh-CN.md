# Nsight 烟雾抓取

## 2026-11-24：quad 空砖整段跳跃（按细步长目标启用）

使用已有每条 ray 的 emptyUntil，取整个 quad 的共同空区间，跳过整数个四步块。
保留 interface 边界、原步数预算和随机序号；仅在 Low-T step scale = 1 且基础步长
主导时启用。跳跃改变浮点加法次序，因此不是逐位一致的采样格点。

RTX 3070、2560×1440、StablePowerState、50 帧中位数；两版相同配置。
下表为 accumulation，不含 upsample。除标明全分辨率外均为半分辨率。

| 配置 | 原版 | 整段跳跃 |
|---|---:|---:|
| 近景，2.5 cm，FP16 单帧 | 4.877 ms | 4.048 ms |
| 近景，10 cm | 2.444 ms | 2.457 ms |
| 近景，50 cm | 1.078 ms | 1.166 ms |
| 烟雾内部，10 cm | 3.264 ms | 3.325 ms |
| 烟雾 + 玻璃，全分辨率 | 6.326 ms | 5.379 ms |
| 高密度烟雾 + 玻璃，全分辨率 | 6.001 ms | 5.026 ms |
| Low-T step scale = 4（跳跃分支未执行） | 4.860 ms | 5.067 ms |
| gameplay mask 关门 | 3.779 ms | 2.936 ms |
| 动画固定 frame 15 | 4.952 ms | 4.157 ms |
| 4-bit stochastic density | 4.707 ms | 3.897 ms |

所有输出有限。固定帧最终图最大差通常为 1/255，stochastic 为 2/255；未跳跃的
内部视角和 scale=4 等配置图像一致。不能据此声称动画、所有边界都完全等价。
细步长获益 15–22%，但粗步长及未执行跳跃的配置仍退步；不能只把分支成本按
实际跳跃次数计算，也未通过 ISA/寄存器分析证明退步的具体原因。最初因粗步长退步
恢复原版；随后按用户要求，以细步长为优化目标，正式启用此候选。quad T 默认开启，
Low-T step scale 默认改为 1，保证固定步长路径能使用跳跃；八点 T 仍默认关闭。
原有 UI 可调整 quad T 和 step scale；没有新增跳跃开关。基础步长沿用场景设置，
JangaFX 为 2.5 cm。对照数据保留在 build/quad-jump、
build/register-pressure/jump-base 与 jump-after。
启用后复测（jump-enabled）：近景 4.038 ms，高密度烟雾 + 玻璃
全分辨率 5.020 ms；两者最终图与原版最大差均为 1/255，
四张积累输出有限。Release 构建、viewer_options / viewer_help 测试通过。

## 2026-11-24：1.5 ms 预算复查

当前版本、RTX 3070、2K、half-resolution accumulation、quad 四步 T、FP16 单帧、
固定 frame 80.5、extinction 6、sun 6 / spot 0、Low-T step scale 1、StablePowerState。
下面是 accumulation 的 50 帧中位数，不含 upsample 或其他 pass。

| 世界步长 | 相机 10 近景 | 相机 11 内部 |
|---|---:|---:|
| 2.5 cm | 4.876 ms | 未重测 |
| 10 cm | 2.443 ms | 3.263 ms |
| 20 cm | 1.595 ms | 未重测 |
| 50 cm | 1.080 ms | 1.099 ms |

50 cm + upsample（约 0.372 ms）已约 1.452 ms，未计体积阴影、消光注入等。
此场景 extinction clear / splat / integration 约 0.76 / 0.24 / 0.50 ms，但这些是
AVBOIT 共享工作，不能不做无烟对照就全部算成体积新增开销。JangaFX 场景选择代码主动
设置 2.5 cm，区别于全局参数的 50 cm 初始值；前面的 4.88 ms 不能称作默认粗步长成本。

更新后的四步版 GPU Trace 中，仅对 `texturedAccumulationHalf` 的非重复函数范围统计
PC samples：步进主函数自身约 22.16%，T 查询含入口约 18.43%，空砖查询/遍历 15.64%，
光照/阴影 19.56%，密度采样 13.49%，逐段积累函数自身 7.31%。这是采样归因，不是
可相加的函数耗时或可直接扣除的毫秒预算。关闭太阳（spot 原本为零），同 shader
2.5 / 10 / 50 cm 分别为 3.953 / 2.154 / 0.973 ms；仍包含 ambient、循环、T、
密度和零强度 spot 的部分几何计算，不能称作完全无光照，也不是可用画质方案。

`--volume-march-stats` 输出经过 half-resolution upsample 后，近景全屏重建均值为：
2.5 cm 时区间 127.36、空区间 109.40、光照求值 13.26；50 cm 时对应 8.01、6.52、1.05。
这些是过滤后的近似统计，不是精确的原生 ray 次数或 GPU 指令计数。当前 `emptyUntil`
只让子区间 continue，仍按四步块推进并查询 T；因此优先探索整个 quad 都证明为空时
推进到下一个可能非空位置。需要保留 brick 过滤支持、各 ray 边界、interface 切分、
采样格点和预算语义，不能直接用单一 lane 的 emptyUntil 跳过整个 quad。

其次考虑密度/光照不同频率、密度 atlas 硬件过滤等；先验证真实跳空，再判断是否需要
画质折中。仅关闭太阳也不能把 2.5 cm 或 10 cm 近景压到 1.5 ms。当前八点 T 和寄存器
小幅优化不足以提供从 4.88 到 1.5 ms 所需的约 69% 缩减。

10 / 20 / 50 cm 与 2.5 cm 静态最终图的平均差为 0.137 / 0.369 / 0.914，最大差为
4 / 10 / 33（0–255）；不把该固定帧对照当作动画或不同相机的质量验收。
本地数据为 `build/register-pressure/budget-current/`，更新后的函数树为
`build/register-current/late-mean/tree.json`。本轮未修改渲染行为。

## 2026-11-24：缩短逐段 T 的存活区间

保留 quad 块的两个 RGB 端点，只传递逐段起止位置的标量插值系数；完成密度解码后，
用系数的平均值计算 meanT，供低贡献光照判定和积累权重共同使用。对于当前线性 T 曲线，
这与两个端点的平均值数学等价；浮点运算顺序会有微小变化。密度采样格点、次数、
interface 切分、T-LUT 查询位置、四张积累 RT 和 pass 数量不变。

当前四步查询 FP16 单帧近景 shader 的 Nsight Live Registers 峰值 **120 → 107**。
调整后 hash 为 `0x02249591dcb95f71`，最高点仍在 VDB 解码。此数值是存活峰值，
不是最终分配数量；没有据此断言消除了 spill 或 occupancy 一定跨过分配档位。

RTX 3070、2560×1440、StablePowerState、50 帧中位数；本轮重新编译原版和最终版交替
复测。默认压力配置沿用下节的 2.5 cm 步长、近景 FP16 单帧、quad T 开启。下表只计
accumulation；全分辨率 case 读取 `accumulation_ms`，半分辨率读取 `volume_accumulation_ms`。

| 场景 / 查询方式 | 修改前 | 修改后 |
|---|---:|---:|
| FP16 近景 / 四步 | 5.164 ms | 4.872 ms |
| 4-bit 近景 / 四步 | 5.305 ms | 4.903 ms |
| 烟雾 + 玻璃，全分辨率 / 四步 | 6.682 ms | 6.339 ms |
| FP16，50 cm 粗步长 / 四步 | 1.150 ms | 1.080 ms |
| 4-bit stochastic density / 四步 | 4.808 ms | 4.691 ms |
| FP16 近景 / 八点 | 4.950 ms | 4.823 ms |
| 高密度烟雾 + 玻璃 / 八点 | 6.307 ms | 6.164 ms |

各查询方式分别与自身修改前版本比较，最终图最大差不超过 1/255，totalTau 完全一致，
四张 accumulation 无 NaN/Inf。该结果不意味着八点模式本身没有其原有近似误差。
普通非 quad 路径最终保留原平均表达式。程序化 gameplay smoke 在通用新写法下由
0.631 ms 退步到 0.689 ms，因此也保留原 RGB 端点写法；使用已有 `SMOKE_MODE` 编译期
分类，不新增 UI 开关或 PSO permutation 维度。
恢复该路径后复测为 0.630 ms（`late-specialized/gameplay`），与原版 0.631 ms 接近；
同一最终构建的 FP16 quad 近景为 4.876 ms，非 quad 近景为 6.234 ms（原版 6.228 ms）。

还单独测试了 density tap 从展开改成循环：FP16 为 4.858 ms、4-bit 为 4.883 ms，
相对只延后 meanT 的收益很小；50 cm case 却从 1.079 ms 退步到 1.101 ms，因此不保留。

本地证据：`build/register-current/late-mean.csv`、同目录 trace 与解析结果；
`build/register-pressure/rebaseline-valid/`、`late-final/`、`rebaseline-eight/`、
`late-final-eight/` 和 `plain-before/`、`plain-after/`。`late-rebaseline/` 因恢复源码时
保留旧时间戳导致 MSBuild 未重编，已明确标为无效，没有纳入表格。
Release viewer 构建、修改 shader 的格式检查及 repository 检查通过；全仓库格式检查
另发现本轮未修改的 Python 文件格式问题，不将它报告为通过。
全仓库格式检查报告问题后长时间未退出，已停止；修改 shader 的独立格式检查通过。

## 2026-11-24：当前 quad 路径的寄存器复查

复用下节相同配置的两份 GPU Trace，在 Nsight GUI 的 Shader Source 中选择
`texturedAccumulationHalf`，导出带 Live Registers 的 HLSL/DXIL CSV。
本轮只诊断，没有修改 shader 或采样策略。

| 位置的 Live Registers 峰值 | 四步查询版 | 八点版 |
|---|---:|---:|
| 整个 shader / VDB 解码 | 120 | 116 |
| LocalVolumeAccumulation.hlsl 源码映射 | 102 | 105 |
| VolumeLighting.hlsli 源码映射 | 100 | 102 |
| DepthMapping.hlsli 源码映射 | 78 | 81 |

这些是 Nsight 报告的程序位置上的存活寄存器数，不是函数独占寄存器数，也不是
PSO 最终分配数量。源码行可能映射多个内联实例；不能把各行或各函数数值相加。
当前没有重现历史版本的 243 峰值，也没有由本次数据证明发生 spill。

最高点对应 `BakedSmoke.hlsli` 的 FP16 voxel 解码和八邻点手工三线性插值。
四步版 DXIL 1492–1594 中多处达到 120；八点版相应区间 1619–1721 达到 116。
这是已有 raymarch 状态与解码临时值叠加后的压力，不能解释为插值函数单独用了 120 个。

DXIL 还暴露了可具体处理的存活区间：四步版在 948–968 行提前计算
`beginT / endT / splitT`，例如 `%813–%815` 在 948–950 定义，直到
2146–2148 才与 splitT 相加得到平均 T。它们跨越了密度寻址、mask 和展开的
密度采样代码。HLSL 里每段最多四个 density tap 展开，每个 tap 又展开八个 voxel。
外层循环有 22 个 scalar PHI，其中 12 个是 N/D/totalTau/BackN 的 RGB 累积量；
八点版外层为 29 个。PHI 数是逻辑状态数量，并不等于硬件寄存器数量。

下一轮优先验证两项保持积分定义的调整：

1. 在密度采样后再由区间坐标计算所需平均 T，减少提前生成的 RGB 端点；低贡献
   光照判定使用同一个平均 T。需检查编译后的存活区间，而非仅移动 HLSL 行的位置。
2. 对比密度 tap 的有限展开和循环，以及三线性插值的分层 lerp 写法，缩短解码
   临时值寿命。减少展开可能降低指令并行度，因此必须同时验证图像、寄存器和耗时。

不改变 RGB 累积格式，不增加 pass，不用减少密度采样数来冒充寄存器优化。
shader occupancy 两版仍约 19%；120→116 的小幅变化尚未带来可见的 occupancy 提升。
静态 instruction-mix 总数为 3888 / 4032，不代表运行时执行次数。

原始 CSV 与解析结果位于本地 `build/register-current/`：`four-step.csv`、`eight.csv`
及同名 `*-parsed.json`。CSV 中 HLSL 和 DXIL 有不同列头，解析时必须在第二个 `#`
表头重建列映射；DXIL 内无行号的源码插入行不是文件分节。
Live Registers 定义参考 [NVIDIA Shader Profiler 文档](https://developer.nvidia.com/docs/drive/drive-os/7.0.3/public/nsight/nsight-graphics/UserGuide/index.html)。

## 2026-11-24：八个均匀 T-LUT 锚点实验

ImGui 的 Volumes 中，先开启 `Quad-shared T-LUT`，再开启
`Eight uniform T-LUT anchors (experimental)`；命令行 `--volume-eight-t` 自动开启 quad 路径。
默认仍关闭。独立 `QUAD_T=2` shader permutation 避免给原版增加运行时分支和缓存寄存器。

每个 quad 使用相同的 view-Z 起止范围，在入口、出口及中间六点查询 T。流式缓存相邻两个
RGB 端点，最多八次逻辑锚点查询；原有逐 ray opaque/Zero-T 边界查询另计。每个四步块的
端点由八点曲线线性插值求出，块内继续使用原来的线性插值。因此跨锚点的四步块会有额外
平滑，并非每一个密度积分点都严格落在八点折线上。密度采样位置、步数预算、interface
拆分、N/D/totalTau/BackN 的定义不变。低 T 自适应步长仍可用，但其输入 T 已是近似值。

RTX 3070，2560×1440，StablePowerState，50 帧中位数；两边均开启 quad sharing。
frame 80.5、camera 10、单帧 VDB，默认步长 0.025 m、Low-T step scale 1、extinction 6、
sun 6、spot 0、半分辨率。玻璃组为 stress 25 / camera 0 / 全分辨率；高密度组 extinction 24。

| 场景 | 原版 accumulation | 八点 accumulation | 最终图平均 / 最大差（0–255） |
|---|---:|---:|---:|
| FP16 近景 | 5.156 ms | 4.957 ms | 0.146 / 5 |
| 4-bit 近景 | 5.308 ms | 4.975 ms | 0.146 / 5 |
| FP16 高密度 | 4.872 ms | 4.737 ms | 1.130 / 19 |
| 烟雾 + 玻璃 | 6.667 ms | 6.569 ms | 0.050 / 5 |
| 高密度烟雾 + 玻璃 | 6.334 ms | 6.313 ms | 0.513 / 106 |
| FP16，50 cm 粗步长 | 1.149 ms | 1.270 ms | 0.024 / 2 |
| FP16，spot 80 | 5.654 ms | 5.434 ms | 0.150 / 6 |

七组 `totalTau` 捕获均完全一致，四张 accumulation RGB 均无 NaN/Inf；没有通过减少
密度积分量换性能。普通细步长场景积累耗时减少约 1.5–6.3%，高密度玻璃组基本无收益，
粗步长反而变慢约 10.5%。整帧未一致变快，例如 FP16 近景为 11.297 / 11.343 ms；
表中收益仅指 accumulation，不将其冒充整帧收益。

画质上普通近景接近，但高密度 solo 的内部明暗有变化；高密度玻璃叠加出现明显的局部亮斑，
全屏平均差掩盖了这种局部误差。因此仅保留供查看，不作为默认质量模式。

第一版在每个小步的起点、终点及界面切分点精确求八点折线，FP16 近景为 5.603 ms，
比基线更慢。当前只在已有四步块边界更新缓存，减少状态推进开销；未保留第一版运行开关。
本地证据位于 `build/register-pressure/quad-eight-base/`、`quad-eight/`（第一版）、
`quad-eight-block/`（当前版）；包含命令、timing、图像和积累捕获。

### 八点为什么只带来小幅收益：同版本 Nsight 对照

重新抓取当前四步查询版（hash `0x57dcea09e29d31c7`）与八点版
（`0x19fd30b46851e030`），相同近景 FP16 单帧配置、2K、StablePowerState、8 次提交。
以下是各自 accumulation shader 内 PC samples 的比例，**不是函数毫秒占比**；
不重复统计父子节点，也不能把这些百分比直接相减换算节省的 ms。

| 范围 | 四步查询版 | 八点版 |
|---|---:|---:|
| T 查询及 Adaptive Z，含初始边界查询 | 17.70% | 5.54% |
| 八点缓存推进/插值函数自身，不含子查询 | — | 5.36% |
| ShadeVolumeQuad 自身 | 25.19% | 28.01% |
| 光照/阴影 | 18.35% | 19.87% |
| 空砖遍历 | 16.00% | 16.73% |
| 密度采样及 mask | 13.28% | 13.40% |

Long Scoreboard 从 30.68% 降至 21.65%，说明依赖读取等待确实减少。
三个 accumulation marker 区间的 L1TEX 命中率为 94.20–94.26% / 97.07–97.08%，
VRAM throughput 为 1.97–2.17% / 1.53–1.72%；两版都没有显存带宽饱和证据。
该 L1TEX 指标覆盖整个 pass，不能称作 T-LUT 专属命中率。

shader occupancy 仍约 19%（19.08–19.37% / 19.04–19.20%），register-limited warp
launch 指标都约 93%，没有因少读 T 而解除并发限制。未得到这两版确切寄存器分配数量，
不能据此宣称寄存器数增加或发生 spill。八点版 L2 throughput 反而从约 4.9–5.0% 升到
12.7–12.8%，来源尚未分解，也不能直接认定是 spill。

结论是查询优化有效，但密度/光照/遍历工作保留，新增缓存推进/插值与持续的低 occupancy
限制了净收益。源码中低贡献光照跳过还依赖 T，八点近似可能改变实际光照调用数量；这次
未单独计数，不能把全部净收益差异归因于该因素。65 次降到 8 次只是 256 步示例，不是
本抓帧逐 ray 查询次数的实测统计。

原始证据：`build/tlut-eight/profile/` 下的两份 `.ngfx-gputrace`、`tree.json`、
`baseline-tree.json` 和 `compare-metrics.json`。

## 2026-11-24：Quad T 查询调度实验

在 quad-shared T-LUT 已开启的基线上，分别测试两个保持采样位置不变的调整：

- A：取消 lane 0 独占查询及 RGB 广播，四个 lane 直接查询相同的 quad 中心坐标。
- B：`Low-T step scale == 1` 时用显式 uniform 分支计算固定步长，切断步长对上一 T 的依赖。

RTX 3070、2560×1440、StablePowerState、每组 50 帧中位数。下表仅为 accumulation，单位 ms：

| 配置 | 原版 quad | A | B | A+B |
|---|---:|---:|---:|---:|
| FP16 单帧，2.5 cm 步长 | 5.309 | 5.164 | 5.252 | 5.143 |
| 4-bit 单帧，2.5 cm 步长 | 5.449 | 5.303 | 5.414 | 5.281 |
| 烟雾 + 玻璃，全分辨率 | 6.842 | 6.665 | 6.794 | 6.649 |
| Gameplay 烟雾弹，50 cm 步长 | 0.647 | 0.632 | 0.644 | 0.630 |
| FP16 单帧，Low-T step scale 4 | 5.288 | 5.131 | 5.327 | 5.198 |
| FP16 单帧，50 cm 步长 | 1.204 | 1.149 | 1.201 | 1.150 |

**只保留 A。** A 的积累耗时减少约 2.4–4.6%；B 对 A 的额外收益很小，且动态步长组变慢，
因此撤回 B 的分支，不保留对比开关。六组 A/B/A+B 的最终图及四张 accumulation RGB
捕获均与原版 quad 完全一致，且无 NaN/Inf。这里的参照是原版 quad，不是逐 ray T 查询。

A 没有减少逻辑纹理查询数；它用同坐标的冗余查询换掉 leader 分支和依赖广播。
此前 Nsight 的高缓存命中率、低带宽利用率支持这一实验方向，但时间测试本身不能证明
硬件合并了多少读取。每四步共享 T 端点、块内插值、adaptive Z 与积分权重均未改变。
quad 开关仍默认关闭，本次未改变其粗步长画质取舍。
清理未使用的 lane 参数后重新构建，FP16 固定细步长复测为 5.159 ms，Low-T scale 4 为
5.130 ms，两组最终图及 accumulation 捕获仍与基线完全一致。

本地证据：`build/register-pressure/quad-schedule-base/`、`quad-all-lanes/`、
`quad-fixed-step/`、`quad-combined/`、`quad-final/`，含逐场景参数、timing、图像及 accumulation 捕获。

## 2026-11-24：开启 quad-shared T-LUT 后的再次抓帧

当前三项默认优化全部开启，另开启 `--volume-quad-t`。保持 2560×1440、FP16 单帧 VDB、
frame 80.5、camera 10、步长 0.025 m、extinction 6、sun 6、spot 0、半分辨率 accumulation。
走 `--benchmark 600` 的 StablePowerState 路径，Nsight timer 延迟 6 秒后采集 8 个提交，
GPU Trace 内实际包含 8 组完整 AVBOIT Frame marker。不是旧 shader 的重放。
`texturedAccumulationHalf` hash 为 `0x86bf21e5d27aa63d`，函数树确实包含 `ShadeVolumeQuad`。

Nsight accumulation marker 为 5.39–5.61 ms（有采集开销）；未注入的上一轮 timestamp 为
5.31 ms。对 accumulation shader 的 2,816,218 个 PC samples 分组如下，分母仅为该 shader，
不是整帧时间；父子函数避免重复计算：

| 范围 | shader 内样本占比 |
|---|---:|
| ShadeVolumeQuad 自身：步进、插值、区间判断、循环控制 | 24.1% |
| T-LUT + Adaptive Z + quad 共享（含初始裁剪查询） | 19.4% |
| 光照及阴影 | 17.8% |
| brick 查表与空区遍历 | 17.1% |
| 密度解码、插值与 gameplay mask | 12.8% |
| 积累权重、坐标等其他工作 | 8.9% |

旧 CBV 抓帧的 T 查询相关样本占比约 45.6%，当前已不再是单一压倒性的热点。
这是不同版本的采样分布对比，不是 T 查询耗时精确减少的比例。
光照的 17.8% 中，普通硬件 PCF 约 3.6%、太阳体积阴影响应约 11.1%；MSM CDF 求解本身
约 2.9%，因此只改 CDF 公式不会消除大部分光照成本。

分别查询三个完整 accumulation marker 内的计数器，得到：

- SM Throughput / Issue：42.1–43.3%；ALU：36.7–37.7%。
- VRAM Throughput：2.04–2.10%；L2 Throughput：5.19–5.31%；L1TEX 命中率：93.08–93.10%。
- 当前 shader 的 warp occupancy：19.4–20.0%。
- `PS Warp Can't Launch - Register Limited`：93.6–96.0%。这是新 warp 启动受限指标，
  **不是 96% 帧时间花在寄存器上**，也不是寄存器 spill 量。
- shader PC samples 中 Long Scoreboard 为 32.8%，Wait 为 29.4%，Branch Resolving 为 5.1%。

判断：当前更像寄存器约束下的低 occupancy、依赖读取延迟和循环控制共同受限，证据不支持
显存带宽饱和。Long Scoreboard 也不能直接等同于 DRAM miss：缓存命中的依赖读取仍可能等待。
原始 buffer 读取 / 三线性解码仍有代价，但不能把 VDB 解压当作唯一瓶颈。
此抓帧 MCP 没有给出可用的 live-state 数量，未把旧 CSV 的 106 Live Registers 当作当前值，
也没有用 occupancy 反推出一个未经验证的寄存器数。

下一步优先级：先缩短 quad 循环中中间状态的存活范围、减少嵌套循环和重复区间判断；
其次评估砖遍历的坐标/边界计算复用（同时警惕新增 DDA 状态又增加寄存器）；然后再对光照
和密度采样做独立降频/存储实验。将一整个 raymarch 区间的 T 只采头尾，不是本次数据支持的
优先方向，也会进一步放大粗步长下已发现的画质误差。

本地证据：`build/quad-profile/quad-current.ngfx-gputrace`、`launch-args.json`、`summary.json`、
`function_scope_tree.json`、`hotspots_shader.json`、`accum-metrics.json`、`markers.json`。
本轮只分析，没有继续修改渲染算法或更改 quad 开关默认值。

## 2026-11-24：DXIL 寄存器压力验证

Nsight Graphics 2026.4 已安装。管理员权限用于采集 GPU Trace 和 PC sampling；
离线查看已有 trace 不需要管理员权限。以下结论来自 11 月 23 日抓帧、24 日导出的 CSV。

用户原抓帧的 `texturedAccumulationHalf`（hash `911d87c219ddec79`）
在 DXIL 第 410 行显示 89 个 Live Registers，第 413 行显示 211 个，峰值 243。
对应循环入口只有 21 个 PHI 状态值，其中 12 个是四组 RGB 累积量。
不能把这个跳变简单解释成循环突然声明了一百多个变量：DXIL 行是驱动机器指令的
关联位置，指令调度和变量存活范围都会影响显示。

旧 DXIL 的灯光 typed SRV 有 101 个固定索引标量分量被读取，包括五套光照/阴影
变换矩阵。这些循环不变量是长生命周期寄存器的主要嫌疑。保留的改动：

- accumulation 按 DDS、FP16 VDB、4-bit VDB、gameplay storage，以及单/双帧插值专用化。
  extinction 和 shadow 继续共享运行时解码器。
- camera raymarch 的灯光数据改为 CBV；其他 pass 暂时继续读同一 buffer 的 SRV。
  原 640 字节数据按 CBV 要求分配 768 字节，没有增加上传或 pass。

新抓帧 hash `2dee05fc051b09a9` 的 DXIL **Live Registers 峰值为 106**，
源码循环行由原来的 218 降为 87。这验证了两项改动共同降低了寄存器活跃量；
尚未分别抓取每项改动的寄存器数据，不能把全部下降归因于 CBV。
Live Registers 峰值也不等同于驱动为每线程分配的寄存器总数。

固定 RTX 3070、2560×1440、StablePowerState、50 个计时帧中位数，单位 ms：

| accumulation 配置 | 改动前 | 仅专用化 | 专用化 + CBV |
|---|---:|---:|---:|
| FP16 VDB 单帧、半分辨率 | 12.168 | 10.392 | 6.592 |
| 4-bit VDB 单帧、半分辨率 | 12.005 | 10.258 | 6.614 |
| FP16 VDB 双帧、半分辨率 | 13.806 | 11.360 | 7.268 |
| 4-bit VDB 双帧、半分辨率 | 13.478 | 11.252 | 7.265 |
| 玻璃 + VDB、全分辨率 | 15.689 | 12.978 | 8.110 |
| Gameplay dense | 1.173 | 1.041 | 0.554 |
| 静态 DDS | 2.418 | 1.865 | 1.022 |

VDB 使用近距离相机 10、128³、帧 80.5、2.5 cm 固定步长、extinction 6，
玻璃和 DDS 使用相机 0；gameplay 使用 50 cm 步长。这是较重的验证配置，
不是默认配置耗时，也不能直接与用户原抓帧的不同场景/时钟比较。
重复单帧 FP16 测得 6.593 ms。已比较场景的最终 8-bit 图像最大差为 1，
四张 accumulation 输出无非有限值；另验证了 stochastic、gameplay buffer/atlas 路径。

合并前后段循环的实验变慢，已撤回；密度 tap 改为动态循环的变化不足 1%，未保留。
本地证据在忽略目录 `build/register-pressure/`：各阶段 `summary.json`、
`command.json`、`scene.png`，以及 `cbv-register.csv`、`register-summary.json`、
`trace-profile/cbv-profile.ngfx-gputrace`。原始 CSV 为用户提供的桌面 `register.csv`。

## 2026-11-24：映射查询、砖状态与硬件 PCF

本轮在上述 shader 专用变体 + lighting CBV 基线上逐项测试，保留三项默认优化：

- **Adaptive Z 打包**：`DepthWarp` 头部仍为 virtualCount、reservedEnd，随后每个虚拟 slice
  使用一个 `uint`：`prefix << 3 | flags`。低三位依次是 occupied、region start、region end。
  查询 prefix 和 occupied 由两次 buffer load 改为一次，整个表从 16,400 B 降为 8,204 B。
  splat、physical occupancy、Zero-T 反查和 CPU/GPU 测试同步使用新 ABI；深度映射不变。
- **砖状态缓存**：已确认非空的 brick，在当前 ray 离开该砖之前不重复查询 indirection。
  空区仍只跳过已证明为空的前缀；双帧时必须两帧都为空。保留原积分步点、密度采样和光照，
  不缓存密度值，也不跨过尚未确认的砖。
- **硬件 PCF**：camera volume accumulation 中普通阴影由四次 Load + compare 改为一次
  `SampleCmpLevelZero`。NVRHI 的 LESS sampler 取补数，保持 reversed-Z 的 `>=` 语义和
  原偏置、clamp 寻址。MSM 不变，其他使用普通阴影的路径保持原实现。

另保留 **Quad-shared T-LUT (experimental)** 供 A/B，默认关闭，命令行 `--volume-quad-t`。
同一 quad 使用共同的 view-Z 步进，每四步在 quad 中心查询一个新的 T 端点，复用上一端点，
块内线性插值。初版由 lane 0 查询并广播；当前改为四个 lane 查询同一坐标，见顶部调度实验。
密度、光照、遮挡和 Front/Back 归属仍逐 ray
计算。已结束的 ray 仍参与 quad 操作，不积累颜色。各 ray 的 interface 深度会切开共享块。
该路径不使用实验性的 T-LUT 相等跳空，不能把它当作逐像素 T 查询的严格等价替换。

测试为 RTX 3070、2560×1440、StablePowerState 成功开启、50 帧 timestamp 中位数。
VDB 使用固定 frame 80.5、近景 camera 10、步长 0.025 m、low-T scale 1、extinction 6、
sun 6、spot 0；这是细步长压力测试，不是默认 0.5 m 步长。玻璃用 stress 25 / camera 0 /
全分辨率；烟雾弹使用默认 0.5 m。下表为 accumulation，不含 upsample、阴影生成等其他 pass。

| 累计改动 | FP16 单帧近景 | 4-bit 单帧近景 | FP16 双帧 | 全分辨率烟雾+玻璃 | 烟雾弹 |
|---|---:|---:|---:|---:|---:|
| 本轮基线（已专用化 + CBV） | 6.592 ms | 6.614 ms | 7.268 ms | 8.110 ms | 0.554 ms |
| + Adaptive Z 打包 | 6.515 ms | — | — | — | 0.547 ms |
| + 砖状态缓存 | 6.307 ms | 6.353 ms | — | — | 0.551 ms |
| + 硬件 PCF（默认） | 6.223 ms | 6.277 ms | 6.834 ms | 7.692 ms | 0.544 ms |
| + quad 共享实验 | 5.312 ms | 5.461 ms | — | 6.838 ms | 0.649 ms |

三项默认优化的 VDB 最终截图与本轮基线一致，烟雾弹个别像素相差 1/255；各组 accumulation
捕获无 NaN/Inf。quad 实验近景 VDB 最终图最大差 1/255，玻璃组最大差 2/255；但粗步长烟雾弹
最大差达到 67/255，且更慢，因此不设默认。四步对应的空间距离随步长增长，插值会抹掉 T 的
快速变化；quad XY 共享也不是透明边缘处的精确重建。静态截图不能替代运动中的质量验收。
这些数据也不能直接外推到其他 GPU；尤其 0.01 ms 量级差异不视作稳定收益。

本地逐项结果和固定参数位于 `build/register-pressure/{packed-map,cached-bricks,hardware-pcf,
quad-secondary}/`，每个 case 包含 `command.json`、`summary.json`、`scene.png` 和有限性检查后的
accumulation 捕获。相关 GPU 回归同时更新了 occupancy CPU oracle 中旧的 bounds.w 语义：
它现在表示拍平 ordering anchor，不能再作为旧的模糊半径字段解释。

## 2026-11-06：Nsight Systems 基线

使用本机 Nsight Systems 2025.1.3，RTX 3070 / 驱动 617.14。没有安装 Nsight Graphics。
配置为 2560×1440、相机 10 近距离、JangaFX 128³ 4-bit 单帧、半分辨率烟雾、
10 cm 步长、TAA、coherent upsample；动画固定在 60.5（实际采样第 60 帧）。
应用 benchmark 开启 StablePowerState，30 帧预热，60 帧计时。

## 抓取结果

`nsys profile --trace=dx12,dx12-annotations --sample=none --cpuctxsw=none
--dx12-gpu-workload=individual --export=sqlite` 可以无管理员权限成功抓取。
`dx12-annotations` 必须显式开启，本机仅指定 `dx12` 时没有 pass marker。
现有 RAII GPU markers 无需改动。

| 范围 | Nsight GPU marker 中位数 |
|---|---:|
| AVBOIT Frame | 10.950 ms |
| Half-resolution volume accumulation | 5.063 ms |
| 其中烟雾 actor 0 / material 0 | 4.995 ms |
| Opaque | 2.268 ms |
| Extinction clear | 0.766 ms |
| Transmittance LUT integration | 0.508 ms |
| Volume depth-aware upsample | 0.375 ms |
| Extinction atomic splat | 0.177 ms |

Marker 统计包含捕获内的 143 帧（包括预热），父子范围重叠，不能直接相加。
同一次抓取中应用自身 60 帧 timestamp 中位数为 accumulation 5.021 ms、
upsample 0.372 ms，与 marker 量级一致。固定时间测试不覆盖连续动画上传。
启动时图形 PSO 创建累计约 6.6 秒是 CPU 启动成本，不是每帧 raymarch 成本。

主要 GPU 耗时确定在烟雾 actor 的 accumulation；不是半分辨率重建。
但这些计时不能进一步证明它是 ALU、缓存延迟、带宽还是分歧受限。

## 硬件计数器限制

Nsight 查询 GPU metrics 返回 `ERR_NVGPUCTRPERM / Insufficient privilege`。
准备了仅本次运行提升权限的 capture 脚本；Windows UAC 启动最终返回
“操作已被用户取消”。没有修改全局驱动权限，也没有取得硬件计数器。
因此本次没有 SM/TEX/DRAM 利用率、occupancy 或 shader stall 的实测结论。

下一次可在允许管理员运行后抓取 `ga10x-gfxt`，或用 Nsight Graphics
Shader Profiler 定位具体指令/源码热点。NVIDIA 官方权限说明：
https://developer.nvidia.com/ERR_NVGPUCTRPERM

## 结合源码的待验证方向

- `SampleSmokeFrame` 依赖 brick indirection 寻址，每次密度采样手工解码八个
  voxel 并插值；4-bit 仍有解码/寻址开销，资产缩小不代表采样指令减少。
- 空 brick 跳过密度和光照，但仍按原步进格点迭代、查询 T-LUT；不能把它当作
  一次直接跨越整个空区的 ray 跳跃。
- 非空区存在 T-LUT、密度、阴影等依赖读取。应以计数器或受控单项替换实验区分
  依赖访存延迟与计算成本，而不是凭总耗时认定 DRAM 带宽不足。

原始抓取（本地忽略目录）：
`build/nsight-smoke/annotations/near-annotations.nsys-rep`、同名 SQLite；
同目录 `command.json`、`marker-summary.json`、`native-benchmark.csv`。
准备好的管理员抓取脚本为 `build/nsight-smoke/capture-admin.ps1`。
