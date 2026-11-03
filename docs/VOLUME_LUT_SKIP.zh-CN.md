# T-LUT 空区间跳过实验

Quality 的体积选项里启用 `T-LUT empty interval skip (experimental)`，或命令行 `--volume-lut-skip`。默认关闭。本版替换此前未获净加速的自适应细分实验，固定步长、jitter、低 T 放大及总步数预算保持原样。

复用每个 raymarch 区间已有的 beginT/endT；当 RGB 三通道逐一完全相等、且三通道都大于零时，直接推进到 endZ，不进行该段密度采样、光照计算和 accumulation。没有额外 LUT 查询、RT 或 pass。任何通道为零时不跳过：饱和平坦区不等于空区间，而且 accumulation 仍需写 totalTau。

这只是有损启发式。RGBA8 的量化、低分辨率 XY 采样和 extinction 注入漏采，均可能让包含介质的区间呈现完全相等的 T。相等比较没有 epsilon，也不能据此证明当前 actor 内部为空。跳过会漏掉该段 totalTau，改变最终不透明度；不能用全局 T 的差值补当前 actor 的密度。

## 结果

RTX 3070，2560×1440，StablePowerState，80帧 ABBA 顺序；计数诊断关闭时测量 accumulation GPU timer：

| 场景 | 原版 | 跳过版 |
|---|---:|---:|
| 烟雾 extinction=3 | 0.472 ms | 0.396 ms |
| 烟雾 extinction=16 | 0.465 ms | 0.404 ms |
| Sponza 动态光烟雾 | 0.683 ms | 0.598 ms |

循环步数不变，但普通烟/浓烟约 63%/54% 的区间跳过密度采样；非零密度段的平均着色次数分别从 1.47/1.46 降为 1.01/1.23。两者不可混为同一个比例。

静态检查覆盖 extinction=.1/3/16/128、磨砂交叉、半分辨率。均无 NaN，但薄边缘出现可见的块状缺口。相对未跳过版本，extinction=3 的最大不透明度偏差约 0.070，extinction=128 的极少数像素达到约 0.124。平均差异小不能掩盖局部瑕疵，因此保留实验开关，默认关闭；没有验证运动/TAA 稳定性。

运行 `avboit/tests/check_volume_lut_skip.py` 可生成 `build/volume-lut-skip/report.json` 及截图。`--volume-march-stats` 将 numerator RGB 临时用于输出总区间数、非零密度段着色次数、跳过区间数，并排除普通透明表面的 numerator；诊断图不能用于画质或性能评估。原始 benchmark CSV 记录完整命令与 StablePowerState 状态。
