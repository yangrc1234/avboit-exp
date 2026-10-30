# 局部体积与动态阴影

启动 `bin/avboit_viewer.exe --sponza --stress 24` 看烟雾；`--stress 25` 加磨砂玻璃。
ImGui 的 **Scene → Lighting** 可以实时调整方向光、聚光灯和 MSM。
聚光灯默认随时间移动；总动画暂停时也会停止。

相机方向仍走 AVBOIT：体积的完整 bounds 参与 occupancy，低分辨率绘制注入消光，
全分辨率 raymarch 查询 T LUT，把散射贡献累加到原来的四张 RT。没有单独的体积合成。

每盏灯有两类阴影：

- **普通 shadow map**：记录 Sponza 不透明／Masked 几何深度，Reversed-Z。
- **体积 MSM**：记录局部体积的总光学厚度和四阶 power moments，重建任意深度的透射率。
  方向光用两级 cascade，聚光灯用透视投影；每级 256×256。

体积采样点的入射光乘上普通阴影可见度和 MSM 透射率，所以既有建筑遮挡，也有烟雾自阴影。
Sponza 的表面直接光照也会查询 MSM，接收烟雾投影。
根据世界空间体积 bounds 判断灯／cascade 是否需要 MSM，不剔除镜头外的投影体积。
没有相关体积时跳过生成和查询；没有体积的场景不分配完整 MSM 数组。

MSM 使用 **RGBA16_UNORM＋R16F**，三层共约 **1.875 MiB**。
先以 FP32 积分，再归一化并使用 MBOIT 作者的四阶 moments 量化变换写出；
读取时两张纹理各做一次硬件 bilinear，再解码。没有手动四 tap 重建。
归一化 moments 和总光学厚度独立滤波，在光学厚度突变处是近似。
静态烟雾、移动聚光灯和磨砂交互三组 2K 截图相对旧 FP32 基线最大差异为 1/255。
当前每层仍为 256×256；没有同时修改分辨率。
当前是标量体积阴影、各向同性单次散射和固定环境项；暂不支持点光源 cubemap、
透明玻璃向灯光投影，也不把测试用不透明 fixture 绘制到 Sponza 阴影图中。
更完整的算法、资源说明和验证方法见 [英文说明](LOCAL_VOLUMES.md)。

初步性能记录：RTX 3070，2560×1440，case 24，默认 packed RT，StablePowerState，
120 帧中位数，详细计时开启。整帧约 7.27 ms，accumulation 约 3.58 ms，阴影阶段
（普通聚光灯阴影＋体积 MSM）约 0.22 ms。跳过空密度光照查询、避免非过渡区重复
查询两个 cascade 前，accumulation 约 8.30 ms；优化前后测试截图逐像素一致。
这是单机原型数据，不代表低端显卡预算已达标。


## 半分辨率 accumulation

ImGui 的 **Quality → Volumes** 中可切换 `Half-resolution local volumes`，或使用
`--volume-half-res`。当前仅局部体积材质提供低分辨率 shader；interface 与其他透明材质
始终全分辨率。渲染器通过 `MaterialShaders::halfResolutionPixel` 显式选择可降分辨率材质。

顺序为半宽半高体积 accumulation → 深度感知 upsample 覆盖全分辨率四张 accumulation RT
→ 全分辨率透明对象 additive accumulation → 原有统一 resolve。Extinction/LUT 不改变。
低分辨率没有绑定全分辨率 DSV，shader 在选定的全分辨率 guide 像素读取深度并裁剪射线。
上采样对 N、A、totalTau、BackN 使用相同样本与权重。
没有全分辨率补算，细遮挡与界面边缘允许近似。
禁用后释放四张半分辨率 RT。2K packed 格式额外占用约 14.06 MiB（不含驱动对齐）。

Upsample 使用 pixel shader，复用 occupancy pass 生成的 `ScreenTiles` 透明标记，
按 64×64 屏幕 tile 绘制；空 tile 在共享 resolve VS 中折叠，不增加 mask、列表或调度 pass。
全分辨率四张 accumulation RT 先清零，未绘制的 tile 保持零，随后全分辨率 actor 再叠加。
这个 mask 是透明 actor bounds 的保守覆盖，包含其他透明 actor，不是逐像素可见烟雾；
覆盖全屏时不会获得 tile 剔除收益。

GPU timer 拆成互不重叠的四项：`accumulation_clear`（全分辨率 RT 清零）、
`volume_accumulation`（半分辨率 RT 清零与绘制）、`volume_upsample`（重建）、
`accumulation`（全分辨率 actor 绘制）。与旧 CSV 对比时，需要将四项相加；
旧版 `accumulation` 包含了全部这些开销。关闭的阶段记录空计时间隔，支持实时切换。

启用半分辨率和 TAA 后，使用四个分层蓝噪声 point taps，每个样本独立计算
“实际采样 texel 距离的 Gaussian 空间权重 × 场景／interface 深度相似度”。
一个候选不匹配只降低自身权重，不再让整个像素退回单点。
只有权重总和接近零时才选择深度最匹配的候选；深度差相同则优先空间最近的样本。
四张 RT 共用归一化后的权重。采样半径固定为一个半分辨率 texel，
不生成深度矩，不增加 RT 或体积 history，也不查询 T-LUT。
SceneDepth 仍包含 Zero-T，因此其人工深度台阶仍可能影响权重；这个改动不消除深度本身的歧义。
上采样跟随 TAA 自动选择，不再单独提供 ImGui 对比开关。
没有 TAA 时使用固定四邻居与 bilinear 空间权重；CLI 参数 `--volume-upsample-bilateral` 仅用于自动化对照。
`Temporal volume dithering (requires TAA)` / `--volume-temporal-dither`
只改变 raymarch 的时域采样相位，用于接入宿主自己的 TAA。
旧参数 `--volume-stochastic-upsample` 仅保留为别名，不再抖动 upsample 地址。
32×32 rank tile 由 `tools/generate_volume_blue_noise.py` 确定性生成，无第三方素材。
本示例可启用下述最终 HDR TAA，但没有独立体积历史缓冲。单帧与有限性检查不代表
已验证 UE4 TAA / DLSS 的时域质量，需在目标工程中测试移动、显露和动态灯光。


## Accumulation 步长与抖动

主视角 accumulation 采用独立的世界空间步长，默认 0.5，不再逐个 Z bin 采样。
每步起点查询 T-LUT，RGB 最大透射率低于 0.1 时平滑增大步长，T=0 时达到 4 倍。
默认最多 256 步；长射线会增大有效步长覆盖完整区间，不会直接丢弃剩余光学厚度。
Interface 仍切分前后段。每个 2×2 quad 的四条射线分配四个步内深度相位，层内用共享蓝噪声偏移，积分区间与长度不变。
半分辨率模式改用逐像素蓝噪声相位，避免高消光烟雾重建后暴露固定 quad 的横竖排列。积分区间与长度不变，没有新增重建 pass。
默认只做稳定的空间抖动；启用 TAA 后随帧改变采样相位，并累积最终 HDR 图像。
每段采用 4 次分层密度采样和 1 次光照计算；可见性使用区间两端 T 的梯形平均，
避免单个随机点落入 T=0 区域就丢掉整段的可见散射。相邻步复用共享端点的 T 查询。
这仍是数值近似，并非逐段精确 Beer 积分。

Zero-T 深度只终止后续步，不挪动最后一步内的密度／光照采样点。
若截止位置的 T 已归零，最后一步保留原来的完整区间；普通不透明表面且 T 非零时仍裁剪区间。
否则开关 Zero-T 会改变前方仍可见烟雾的积分位置，浓烟底部尤其容易出现黑斑。
可用 `--volume-extinction 64` 或 `128` 重现高消光场景。

ImGui 的 **Quality → Volumes** 可实时调整步长、低 T 阈值、最大倍率和步数预算；倍率设为 1
即禁用低 T 加速。对应命令行：`--volume-step`、`--volume-low-t`、
`--volume-step-scale`、`--volume-max-steps`。不新增 RT 或 pass。
大步长会近似密度／光照和 totalTau，可能漏掉细结构。Extinction 注入仍逐 Z bin 积分；
光源侧 MSM 仍使用原有独立的 96／32 步。


## 选择 actor 和调整材质

**Actors** 页签统一列出玻璃、烟雾、不透明测试物体／发光球、VFX、太阳和聚光灯。
可以搜索列表，或左键点击场景中的 bounds 选中。每个几何 actor 有独立材质，修改不会影响其他 actor。
位置、XYZ 角度、Move／Rotate gizmo 和 Enabled 开关立即生效；Reset transform 恢复初始变换。
灯光朝向由旋转控制；聚光灯还可以调整强度和动画。发光球可编辑 HDR Radiance，VFX 可编辑强度和图案。

烟雾 **Extinction** 同时调整 RGB 消光系数（0～128 / world unit），**RGB extinction** 支持分通道编辑。
体积旋转使用同一组 OBB 轴处理相机光线、密度采样和 MSM 阴影；occupancy 使用旋转后的保守 AABB。
玻璃的颜色、透射率、粗糙度、IOR、折射位移仍在同一编辑区。
**Shared material defaults** 收纳共用折射默认值、磨砂细节偏移和法线 tiling。

画质切换保留 actor 编辑。Scene 的 fixture／场景预设用于重建测试布局，会重置编辑；
导入的 Sponza 建筑仍作为背景场景管理，不逐个拆成测试 actor。


### Demo TAA

`--taa` 或 ImGui 的 **Quality → Display → Temporal anti-aliasing** 启用 Donut TAA，与相邻 ADS demo
使用同一个库实现。TAA 在透明合成与 VFX distortion 后、tonemap 前处理 HDR；
Halton 投影 jitter 同时用于 opaque、透明几何、occupancy 投影和体积射线。
启用时自动开启体积采样相位变化：全分辨率轮换 quad 相位，半分辨率偏移逐像素蓝噪声；半分辨率默认使用上述蓝噪声重建。
关闭时释放 TAA history、motion 和 output RT；默认关闭，便于保留无 TAA 的算法对比。

当前 motion 由场景深度与前后帧相机生成，支持相机移动，但没有透明物体自身的运动矢量，
也没有为体积积分结果定义独立代表深度。移动透明物体、烟雾与折射区域仍可能拖影；
这是用于观察采样收敛的基线，不是完整的透明 temporal reconstruction。
切换视角、重建资源和编辑 actor 材质会重置 history。GPU profiler 单独列出 `taa`。

这里借鉴的是 [RDR2 第 56 页及备注的最终上采样](https://advances.realtimerendering.com/s2019/slides_public_release.pptx)。
不需要前面第 51–55 页的缺失射线历史重建；我们每帧计算全部半分辨率像素。
按当前取舍，没有实现加权 ray depth 控制 kernel 宽度。原分享没有公布精确点集与阈值，
四象限分层点集、2%／2cm 深度尺度是 demo 的选择；也改用了逐样本 bilateral 权重，
而不是分享中的深度不连续处选单点，不能称为逐项复现。
原方案过滤已积分散射／透射率；我们过滤 AVBOIT 中间量后再非线性 resolve，两者不严格等价。
低分辨率没有合适深度样本时仍可能漏光或闪动，最终 TAA 也没有独立的体积运动矢量。

`--sponza --stress 24 --volume-extinction 64 --volume-upsample-test` 可输出静止／移动镜头的逐帧对照。
测试脚本统计固定烟雾 ROI 的时域变化，并检查无 TAA 时回退 bilateral、磨砂交错时输出有限值。

`python avboit/tests/check_dense_volume.py` 在 2K、packed RT 下检查高消光 Zero-T 开关差异、
底部零权重黑洞和半分辨率 TAA 有限值。仍允许少量孤立像素的截止差异，
不能据此宣称任意运动下的时域质量已经通过验证。
