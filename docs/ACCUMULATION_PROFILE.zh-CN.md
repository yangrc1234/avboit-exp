# 烟雾 accumulation 消融测试

2026-11-15，RTX 3070，DX12 StablePowerState。结论：重复光照/阴影评估与密度查询是主要热点；T-LUT 查询有成本，但不是唯一或最大的来源。不能把这里的时间差解释成纯显存带宽占比。

## 条件

- 输出 2560×1440，烟雾 half-resolution accumulation（1280×720），coherent upsample 单独计时。
- Sponza stress 24，JangaFX Smoke_Plume_01，128³、4-bit sparse bricks，固定第 60 帧、单帧采样。
- camera preset 10：靠近烟雾；11：烟雾内部。extinction=6，sun=6，spot=0。
- gameplay mask 是全一，但原 shader 仍会读取该贴图。TAA 开启。
- 主实验步长 0.1 m，低 T 步长倍率 4，最大 256 步；另测默认 0.5 m。
- 每个进程预热 30 帧、记录 60 帧 GPU timestamp 的中位数。捕获和读回在计时之外。
- 总计 36 组结果。近景基线复测偏差约 0.5%，内部视角约 4.3%；小于这类波动的差异不做强结论。

## 0.1 m 主实验

以下都是 **volume_accumulation_ms**，不含 extinction、阴影图生成、upsample、resolve 或整帧其他工作。

| 替换项 | 近景 ms | 内部 ms |
|---|---:|---:|
| 完整管线 | 5.037 | 6.711 |
| 去掉全部光照，保留密度与 T | 2.803 | 3.318 |
| 体积 MSM 查询返回 1 | 4.392 | 5.119 |
| 不透明阴影查询返回 1 | 4.636 | 5.486 |
| 两类阴影均返回 1 | 2.884 | 3.491 |
| 每段强制一次密度查询 | 3.752 | 4.315 |
| 省略全一 gameplay mask 查询 | 4.654 | 5.858 |
| STF：八体素插值改为随机单体素 | 4.660 | 6.279 |
| 去掉零强度 spotlight 的几何计算 | 4.228 | 4.987 |
| 保留阴影贴图读取，简化 moment 求解 | 4.942 | 6.270 |
| 去掉光照，密度直接返回常数 | 1.183 | 1.008 |
| 关闭 brick 跳空 | 7.706 | 7.527 |
| 完整管线复测 | 5.014 | 6.425 |

### 密度查询

每段根据跨越体素的 footprint 选 1/2/4 个查询位置；每个位置还要读取 gameplay mask、brick indirection 和量化范围，并手工解码八个体素完成三线性插值。

近景只去掉光照为 2.803 ms，进一步把密度替换成常数为 1.183 ms。内部对应 3.318 → 1.008 ms。这个差值包含密度相关坐标、tap 选择、mask、解码、插值和分支变化，不是仅八次 buffer load 的成本。常数密度也会改变零密度返回路径，因此只是消融证据，不是独立的模块计时。

强制一次密度查询在近景省约 1.28 ms，在内部省约 2.40 ms，但会降低积分质量，尤其较大的步长。STF 的收益明显小于去掉多次密度查询：它仍保留坐标、mask、indirection、随机数生成和外层循环。不能据此认定换成 4-bit 或单体素读取就能解决全部性能问题。

### 光照与阴影

去掉全部光照在近景省约 2.23 ms。两类阴影同时省掉的收益也很大，但不能把单独移除各自的差值相加：opaque visibility 会控制 MSM 是否执行，shader 的分支、寄存器分配和调度也可能变化。

另发现即便 spotlight 强度为 0，原 EvaluateVolumeLighting 仍计算距离、锥角、range fade 等。编译时去掉这些计算，近景 5.037 → 4.228 ms、内部 6.711 → 4.987 ms。tau 和 denominator 逐字节一致；最终画面最大差 1/255、全屏 MAE 小于 0.001/255，属于少量浮点舍入差异。这个结果支持优先跳过不参与光照的灯，但编译期消融的收益不等于添加运行时 if 后一定能获得的收益。

保留 moment/tau 两个硬件过滤读取，仅用一阶近似替代四矩求解，近景变化很小。该诊断仍依赖 moment 数据，避免贴图读取被编译器删除，但会减少部分解码计算；它不是正确的 MSM 替代方案。不能把整个光照热点归因于 MomentCdf 的公式本身。

### T-LUT

为了不把步长变化误认为查询成本，单独关闭光照、固定 step scale=1、禁用 exact-T skip，再去掉循环内 T 查询。初始 stopZ 的查询仍保留，以保持 Zero-T 截断语义。

| 固定步长、无光照 | 近景 ms | 内部 ms |
|---|---:|---:|
| 保留循环内 T | 2.819 | 3.326 |
| 循环内 T 返回 1 | 2.098 | 2.835 |

这表明重复的 log-Z 映射、DepthWarp 读取与 3D T-LUT 采样值得优化。此处差值约 0.72/0.49 ms，不能直接加到主实验的其他差值上；T 返回 1 后也简化了部分加权运算。

### 跳空

关掉 brick 跳空明显变慢。当前实现只跳过空区间中的密度/光照，仍以原始 integration lattice 前进并查询 T，而不是把 ray 直接跳到下一块。这个行为留下循环优化空间，但现在的跳空整体有收益，不应直接删除。

## 默认 0.5 m 步长

| 替换项 | 近景 ms | 内部 ms |
|---|---:|---:|
| 完整管线 | 1.960 | 2.057 |
| 去掉光照 | 1.126 | 1.065 |
| 每段一次密度查询 | 1.639 | 1.498 |

默认步长下同样是光照的重要性最高，其次是密度查询。upsample 另约 0.372 ms，以上数据尚不满足“完整烟雾路径 1.5 ms”的目标。一次密度查询仅是质量会变化的性能下界测试，没有设成默认。

## 可复现与检查

正常 shader 不启用任何 audit。`--accumulation-audit` 只选择独立的离线 PSO，不向 ImGui 增加质量开关。默认渲染行为保持不变。

准备 [JangaFX 4-bit sequence](BAKED_SMOKE.zh-CN.md) 并构建 viewer 后，在仓库根目录执行：

```powershell
python avboit/tests/profile_smoke_accumulation.py
python avboit/tests/profile_smoke_accumulation.py --extra
python avboit/tests/profile_smoke_accumulation.py --coarse
```

输出在 `build/accumulation-audit/`：每组 command.json、run.log、GPU timestamp CSV、最终画面，以及 summary.json（中位数、P10/P90、校验哈希）。大体积临时原始贴图检查后删除。

审计模式：1 无光照；2 无 MSM；4 无 opaque shadow；6 无两类阴影；8 单次密度查询；16 无 gameplay mask 查询；33 常数密度且无光照；65 固定步长无光照且循环内 T=1；128 移除零强度 spotlight 的计算；256 简化 moment 求解。正常模式为 0。

脚本检查所有 accumulation 输出有限且非负，所有变体的 extinction/T-LUT 哈希相同。改变光照的变体和移除全一 mask 的变体还检查 tau/denominator 一致；移除全一 mask 和基线复测检查所有 accumulation 输出逐字节一致。CLI 禁止 T 消融搭配自适应步长/LUT skip，禁止诊断 PSO 与 STF 混用。

这些是完整 pass 的编译期消融，差值包含编译器重排和潜在寄存器压力变化，不能作为可相加的饼图。尚未取得 shader 硬件性能计数器，因此没有把热点进一步断言为 DRAM、cache miss 或 ALU 吞吐受限。
