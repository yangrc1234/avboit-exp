# 当前透明管线

这里描述发布代码的实际执行路径。算法局限见 [KNOWN_LIMITATIONS.md](KNOWN_LIMITATIONS.md)。

## 一帧顺序

```text
Opaque / SceneDepth
Interface prepass
Bounds occupancy + frost masks → Adaptive Z → Physical occupancy
Extinction clear → Atomic splat → T-LUT integration
Zero-T indirect depth draw（直接写主 SceneDepth）
4RT OIT accumulation
B′ resolve（应用玻璃折射）→ Gaussian mip chain
Main composition → Tile copy 回 SceneColor
VFX offset → VFX HDR apply → Display / tone mapping → ImGui
```

| 数据 | 默认格式 | 作用 |
|---|---|---|
| SceneColor | R11G11B10 | Opaque 输出、最终透明合成目标 |
| SceneDepth | D32 | Opaque 深度；之后被 Zero-T quad 原位修改 |
| InterfaceDepth | D32 | 每像素最近特殊界面，深度测试选面 |
| InterfaceSurface | RGBA16F | RGB 透射率 + frost sigma |
| InterfaceOffset | RG16F | 选中玻璃的材质折射偏移 |
| Extinction | packed uint buffer | scalar 每 DWORD 四个 slice；RGB 三个带 guard 的 10-bit 字段 |
| T-LUT | RGBA8 3D | 积分后的透射率，默认 XY 1/8、Z 128 |
| FirstZeroSlice | R32_UINT，extinction XY 大小 | 第一个 RGB 全零 slice，供 Zero-T depth VS 查询 |
| Zero-T candidates / draw args | uint 列表 + 16 字节参数 | integration 输出候选 16×16 tile，驱动一次 indirect draw |
| N / A / totalTau / BackN | 四张全分辨率 R11G11B10 | OIT 总贡献、权重、光学厚度、界面后方的 numerator |
| B′ | RGBA16F | 透明 tile 内已折射的背景颜色 + 有效性；未覆盖域不读取 |
| Gaussian | RGBA16F mip chain | 预乘有效性的过滤结果 |
| TemporaryColor | 与 SceneColor 同格式 | 主合成临时输出，随后 tile 拷回 SceneColor |
| VFXOffset | 全分辨率 RG16F | 多个 VFX 的加法偏移 |
| VFX HDR | 与 SceneColor 同格式 | 独立后期 apply；无 VFX 时释放并跳过 |

没有单独存储 q / I / R，也没有 front/back 各三张 RT。
`--accum-half`、`--composition-half` 保留 RGBA16F 数值对照。

场景、interface 和阴影的 D32 都使用 Reversed-Z：近处为 1、远处/清屏为 0。
深度测试使用 Greater/GreaterEqual；interface 选择使用严格 Greater，让相同深度保留先写入的表面。
主相机保留 0.05/80 的有限近平面与远平面；`deviceDepth=A+B/viewZ`，
其中 `A=-near/(far-near)`、`B=near*far/(far-near)`。AVBOIT 仍按正的 view Z 从近到远积分。

## AVBOIT

CPU 提交保守 actor bounds 与材质信息。共享 compute 同时标记普通透明、frost 的 XY 区域和
2048 个虚拟 log-Z 区间。Adaptive Z 根据 occupancy、guard 和 prefix scan 压缩深度，
超过预算时继续合并；Physical occupancy 把虚拟 mask 映射到物理 slice。

低分辨率光栅 splat 用整数原子累加量化光学厚度，按深度小数部分分配到 Z 和 Z+1；
XY 使用光栅覆盖。这对应原讲稿第 64–65 页的 Linear Splatting，不要求额外 XY scatter。积分得到 RGBA8
T-LUT。**最终 accumulation 仍然全分辨率。** LUT 查询使用线性过滤，并保留虚拟深度采样偏移。
packed integer 有有限 guard 容量；极端 RGB overdraw 的跨字段 carry 是已知近似。

Integration 顺便输出代表 ray 已 RGB 归零的候选 tile，并原子递增 indirect instance count。
每个 tile 只有一条 owner ray，不需要去重 pass。计数随 extinction clear 每帧重置，列表只读取当帧有效前缀；
2560×1440 下列表容量 57,600 字节，draw args 16 字节。
Zero-T VS 再查询完整过滤 footprint 的 first-zero 信息，保守取最远零点并反解深度，保证跨 compute group 邻域也已写完。
这与讲稿第 78 页的独立 quad-generation CS 不同：本实现由 integration 产生候选，VS 求最终深度。
讲稿要求保守读取，但未给出确切 XY footprint；当前外扩 guard 是本实现的选择。
无效 tile 折叠 quad；有效 tile 无 PS 地写入主 D32，没有 quad preparation 或独立 culling depth。
后续 accumulation 使用该深度做 early depth rejection。Resolve 在**当前 SceneDepth** 处查询过滤后的
T-LUT，使用与 accumulation 相同的 adaptive Z 和两虚拟 slice 偏移；RGB 全零时，将有效 `totalT` 设为零，
并用这个值计算 `(1-totalT)/A`。不修改存储的 `totalTau`，不占 stencil，不新增 RT 或 pass。

B 生成与最终合成共用这个判断；闭合的射线跳过 opaque 颜色读取，允许 deferred 宿主不计算其背后的光照。
位于闭合点前方的玻璃仍按自身深度求 q，不能因为远处不透光就把 q 一起清零。
该规则依据逐像素 LUT，而非记录 quad 是否实际写入，因此也可能闭合保守 quad 覆盖外的全零像素。
不能直接读取 LUT 最远端，因为不透明表面可能比浓烟更近。它仍受 LUT 的空间和量化近似影响；
`--no-zero-depth` 同时关闭此修正，用于未剔除、按全分辨率 totalTau 合成的参考路径。

## 界面选取与四 MRT

所有需要 frost / 自身折射的表面提交 interface prepass，每个 actor/material section 独立 draw。
Depth test 选择最近表面，同时输出这张表面的 T、sigma、offset。普通零 sigma/零 offset 透明材质
不占特殊界面。被 opaque 遮挡的特殊表面深度仍保留，用于后续排除 frost 的前景源。

随后只做一遍完整 accumulation。每个片元正常写 N、A、totalTau；根据 InterfaceDepth，
界面后方的片元额外写 BackN，前方/界面自身写零。几何球使用单面剔除，后半球不作为额外透明层。

```text
totalT = exp(-totalTau)
if zeroDepthEnabled && all(T_LUT(SceneDepth) == 0): totalT = 0
k      = (1 - totalT) / max(A, epsilon)
I      = N*k + Opaque*totalT
q      = max(totalT, saturate(T_LUT(interfaceDepth)*interfaceT))
B      = (BackN*k + Opaque*totalT) / max(q, epsilon)
Front  = (N-BackN)*k
D      = InterfaceOffset(p)
detail = sigma > 0 ? clampLength(D, detailRadius) : 0
Bd(p)  = SampleResolvedBackground(p + D - detail)
Result(p) = Front(p) + q(p)*Filter(Bd)(p + detail)
```

RGB 逐通道运算。直接累加 BackN 避免用大数相减恢复很小的背景贡献。
q 的全分辨率 totalT 下界避免低分辨率 LUT 估计使背景除法异常放大；q 在寄存器中重算。
Gaussian 只过滤已折射的背景 Bd，界面前的烟雾保留在目标像素的清晰 Front 中。
源位置的 B 使用源位置自己的 q，最后乘回的 q 则属于目标像素，不能混用。

## B′ 与区域跳过

B′ 是 RGBA16F RenderTarget，未启用 UAV。每帧生成前通过 `ClearRenderTargetView`
清为零 RGBA，并在资源创建时声明相同的 optimized clear value；未写区域的有效性因此为零。
调试 poison 模式先填垃圾再执行相同的零清理，验证旧内容不会进入本帧。B 和 occupancy 均不加 1px border。

Gaussian 首级固定只接受 interface 覆盖内的源。每个 tap 先 Gather interface depth 的四个双线性邻居，
任意邻居无 interface（D32 clear=0）就返回零 RGBA，再交给现有对称样本替换。
玻璃折射的前置部分在 B′ 生产时完成；对于带 frost 的目标，偏移位置最近的源 texel 必须有 interface 且未被
SceneDepth 遮挡，否则返回零 RGBA。光滑折射仍允许前景采样。
后续 mip 保持预乘有效性，不增加 RT/pass。
这个保守规则同时排除界面外的真实背景，边缘可能延展内部颜色；
它不区分相邻界面的身份。窄界面或大偏移若使所有样本失效，会沿用现有的未模糊合成回退。
Test case 16 放置玻璃边缘外的 HDR 前景，用于检查边缘污染。

64×64 的 **普通透明 occupancy tile** 内生成完整 B′，不加像素边界。双线性采到未写区域时，零 RGB/有效性参与过滤。
B′ 在目标像素写入已折射背景：直接读取偏移位置的全屏 Opaque、accumulation、LUT/interface，
先硬件双线性过滤连续 RT 输入，再 resolve 一次。不会读取另一位置的 B′，也不需要源位置预先生产 B′。
无偏移时使用精确 texel load；源位置没有透明贡献时直接读 Opaque。
B′ 的生产域不由 frost 或折射半径决定，
因此不用为了 B′ 调度限制效果半径。

主合成不能一边采样原 SceneColor，一边原位写它：先用 tile VS 绘制到 TemporaryColor，
再用相同 tile 拷回 SceneColor。无效 tile 在 VS 折叠。

Gaussian 只保留首级 4×4 输出 group 的 work mask，直接由 frost actor 投影 bounds 标记；
不按 kernel、重建采样或 detail offset 外扩。重叠请求通过原子 OR 合并。
首级 UAV mip 每帧清为零 RGBA，未过滤 group 保持零有效性；后续活跃 mip 全屏生成，不再分配或读取逐级 mask。
**首级是固定 compute 网格 + 每 group 一致 early return，不是 compact list / indirect dispatch。**
调试矩形只是首级 mask 的包络，不代表每个矩形内像素都执行 blur。
这是允许边缘近似的简化：靠近界面边缘的过滤支撑可能减少，不再保证与全屏首级生成逐像素一致。

## Gaussian 与重建

sigma 来自材质粗糙度与 IOR，单位是 1440p 基准屏幕像素，不依赖背景深度。
默认 3 级，在 16:9 下为 320×180 → 160×90 → 80×45。增加到 4/5 级时向精细端增加，
相同宽高比下不随内部渲染分辨率改变；不同宽高比会调整 mip 宽度。

首级直接从 B′/Opaque 过滤生成，sparse 为 16 taps、dense 对照为 64 taps，输出尺寸相同。
后续层融合水平/垂直 Gaussian，水平中间值放在 groupshared，不分配水平临时 RT。
所有级别保持线性 HDR 与 RGB/有效性的预乘关系，没有亮度加权降采样。

一个 tap 无效时可用对称 offset 的 tap 替代；都无效则不贡献。最后读取时才除以有效性。
全无效回退普通 OIT。正 sigma 只从第一个 blur 级开始，通过方差选择 fractional LOD，
不做清晰图与首级的手动混合。Bilinear 是一次硬件 mip 采样；cubic 每级四个 bilinear taps，
fractional LOD 混合两个级别。

## Distortion

玻璃 offset 仍由 interface prepass 写入同一张 RG16F。带 frost 的界面将它拆成两部分：
`detail = D * min(1, r / max(length(D), epsilon))`，B′ resolve 使用 `D-detail`，
主合成使用 `detail` 偏移 Gaussian 采样。Front、RGB q 和 sigma 始终取原像素；光滑玻璃仍全部在 B′ 阶段处理。
`r` 默认是 1440p 下 24 像素，随渲染高度缩放；ImGui 的 `Post-blur offset (1440p px)` 或
`--frost-detail-radius 0..64` 可实时调节。0 表示全部前置，同一个实现自然退化，不保留独立 shader 变体。
该参数不截断总偏移。最终合成增加一次现有 offset 读取；没有新增 RT/pass 或 Gaussian 采样次数。
detail radius 不再扩大 Gaussian 调度区域；最终读取继续按过滤后的有效性归一化，全无效时回退普通 OIT。

这只是同一向量的幅度分配，不是严格的频率分解。恒定偏移可相加还原；空间变化偏移的两次 warp 不可交换，
在曲面或界面边缘仍是近似。Test case 18 并排放置无偏移磨砂和带法线贴图偏移的磨砂，后面有相同亮球。
Interface 材质 shader 采样 [CC0 法线贴图](../assets/textures/README.md)，线性解码 XY 并写入已有 offset。
贴图为 512×512 RG8，含完整 mip；Wrap + SampleGrad 在远处过滤细节。它作为艺术化屏幕偏移，未做完整切线空间折射。
`Frost normal tiling` / `--frost-normal-tiling .25..64` 控制整张贴图在玻璃上的重复次数，默认 1，实时修改不重建资源。
未加入 sigma 噪声或表面明暗噪声，便于单独观察后置折射。

偏移位置的 N/A/totalTau/BackN、Opaque 和 interface RGB T 使用硬件双线性采样，再 resolve 一次。
深度、界面归属与有效性判定取最近源 texel；目标 Front/q 不变。
这不是完全等价的过滤，尤其 `exp(-filter(tau))` 和 `filter(exp(-tau))` 不同，跨界面或无效源边界还会
混合不同含义的数据。若实际 deferred 管线允许 Zero-T 后的 opaque 邻居未初始化，这个近似还要求
保证双线性 footprint 内颜色可读；只验证中心像素为非零透射率不够。
VFX 是独立材质 pass，写全分辨率 RG16F 加法 offset；用 T-LUT 衰减，忽略最近特殊界面后的 VFX。
VFX apply 在透明合成后运行，所以近处 VFX 可以扭曲已经完成的 frost 结果。

B 是相对于**采样源像素自己的界面**定义的，不是相对于每一个接收玻璃统一定义。
因此 sharp refraction 仍可能把接收玻璃前面的物体采进来；屏幕空间方案不恢复隐藏几何。

## 工程边界

`avboit/render` 只依赖 NVRHI；宿主提供材质 shader 集、几何 section、参数和 command list。
资源重建前由宿主等待在途 GPU 工作，pass `Record` 不自行提交或等待。
使用 Reversed-Z D32，内部尺寸对齐到四的倍数；蒙皮、引擎材质系统需显式接入。
RenderDoc 标记使用统一 RAII 宏，GPU profiler 不侵入算法模块。
