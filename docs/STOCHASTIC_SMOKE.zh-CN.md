# 烟雾 stochastic filtering 实验

在 **Quality → Volumes → Stochastic smoke filtering (experimental)** 开关对照，
或使用 `--smoke-stochastic-filter`。默认关闭。切换 shader 变体时重建管线、
保留 actor 编辑并重置 TAA history；没有额外 RT 或 pass。

只替换 accumulation 中压缩 brick 的三线性密度查询：依据小数坐标的三个轴权重，
随机选择八邻居之一并解码。选择概率等于原三线性权重，返回值无需再次加权。
沿用当前 brick 的 5³ padding 和 min/max，避免跨 brick 量化范围改变参考值。
FP16 和 4-bit 均支持；原始 DDS 路径保持硬件过滤。

每个 pixel/frame/step/segment/subtap 使用散列生成三维随机数，与射线位置 jitter
分开。N、A、totalTau、BackN 共用同一随机密度；每步沿射线的 1/2/4 子采样不变。
Extinction splat、T-LUT、MSM 和 gameplay mask 保持原过滤。
双帧模式仍插值两帧密度；本次性能对照使用单帧。

随机密度的期望等于量化资产的三线性密度，但 resolve 包含指数和除法，TAA 包含
history rejection/clamping，最终画面不保证无偏。TAA 关闭时随机相位固定，可能留下
静态颗粒；目前使用简单散列而非专门优化的时空蓝噪声。

## 2026-11-09 实测

RTX 3070，2560×1440，StablePowerState，30 帧预热、60 帧中位数。
JangaFX 128³ 4-bit 单帧，半分辨率 volume，10 cm 步长，extinction=6，TAA，coherent upsample。

| 相机 | 三线性 accumulation | 随机单样本 | 减少 |
|---|---:|---:|---:|
| 10：近距离 | 5.036 ms | 4.655 ms | 7.6% |
| 11：烟体内部 | 6.665 ms | 5.967 ms | 10.5% |

两组 upsample 均约 0.372 ms。初版运行时分支影响了参考 shader 的编译结果，
已改为独立 entry point/PSO，未采用初版计时作为结论。
减少八邻居解码没有带来同比例提速：间接寻址、循环、T-LUT 和光照仍在，
新增 RNG 也有成本；尚未取得硬件计数器，不能断定具体微架构瓶颈。

静态 TAA 画面在烟雾覆盖区域的显示 RGB 平均绝对差分别为 0.478/255 和 0.315/255；
平均有符号差约 -0.007/255 和 +0.021/255。平均颜色接近不代表局部没有噪声。
动画播放和横移镜头捕获中，烟雾边缘明显比三线性更有颗粒感，暂不改为默认。
动态对照的全屏 MAE 约 0.30/255，会被大面积未改变的背景稀释，不能当作烟雾质量分数。

测试确认两组 T-LUT、全部光照 moments/tau 逐字节一致，accumulation 和 TAA HDR
输出均有限、accumulation 非负。脚本：`avboit/tests/check_stochastic_smoke.py`。
本地记录：`build/stochastic-smoke/summary.json`，各子目录 CSV、命令和静态／动态截图；
`comparison.png` 上排为整体图，下排为移动镜头 1:1 crop，左三线性、右随机过滤。

相关方法：[NVIDIA STF](https://research.nvidia.com/labs/rtr/publication/pharr2024stochtex/)。
本次为独立的 3D 线性核随机采样实现，没有引入 RTXTF 库或 quad 样本复用。
