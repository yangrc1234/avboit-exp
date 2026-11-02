# 同模型内外壳测试

在 Test case 中选择 `Layered model / noise shell (0, 1, 5 cm)`，或运行：

```powershell
.\bin\avboit_viewer.exe --stress 26
```

左、中、右三组使用同一个带凹凸的封闭模型。每组两份相同拓扑：橙色条纹内层、蓝色 noise opacity 外壳。外壳分别沿法线外推 **0、1、5 cm**。这是几何间距，不是 OIT camera offset。

每一层都是独立 actor、材质和 draw，剔除背面。两层均走正常 AVBOIT extinction splat 和 accumulation；没有根据提交顺序强制让外壳盖住内层。完全重合组故意复现传统 alpha blend 依靠“先内层、后外壳”才能确定覆盖关系的情形。

在 Actors 中选中 `Noise shell`，调整 `Coverage`、`RGB transmission`、`Noise frequency`；关闭对应内层或外壳可分别观察。Noise 使用固定的模型空间坐标，移动或旋转 actor 时不会滑动。默认关闭这两层的 frost 和 distortion，避免混入其他效果。

观察外壳 noise 接近不透明的位置是否仍泄漏内层的橙色或条纹，以及增加几何间距是否缓解。Noise 的过渡区本来就应该混合，不能把所有混色都当成排序错误。完全重合没有唯一的物理前后关系；本测试的美术目标明确指定外壳在上。尚未实现传统排序渲染对照。

## OIT offset（按 bin）

选择 Noise shell actor，在材质面板调整 `OIT offset (bins)`，范围 -4 到 +4，允许小数，默认 0。正值向相机拉近 OIT 排序，负值推远；各 actor 独立。也可以用 `--stress 26 --shell-offset-bins 2` 为三个外壳设置初始值。建议先观察 +1/+2 bins。旧的厘米参数已删除。

偏移在 adaptive Z 映射后应用：`orderingSlice = mappedPhysicalSlice - offsetBins`。+1 表示向前移动一个最终 LUT slice，不是 2048-cell 虚拟网格的一格，也不是固定世界距离。模型顶点、屏幕覆盖、opaque 遮挡、材质坐标和光照不变。Splat 保留原有物理深度斜率 push，再映射并偏移；T-LUT 查询保留原有采样 bias，再应用同一 bin 偏移。特殊界面前后 numerator 分类也在同一映射坐标中比较；offset=0 保留原来的 device-depth 比较。

Virtual occupancy 使用真实 bounds，避免 offset 与 adaptive 映射循环依赖。CPU 统计启用 actor 的最大正/负有效 offset，各自向上取整。Adaptive Z 在固定预算内为两个方向预留头尾 bins；physical occupancy 对原有 Z 标记按此范围扩展，保证 sparse clear/integration 覆盖所有写入。这里使用全局保守范围，不增加 per-actor mask 或新 pass。代价是占用少量 Z 预算、扩大已占用 tile 内的 Z 工作范围；临界情况下可能触发 adaptive coarsening。全部 offset=0 时不预留、不增加此扩展。固定 Z 参考路径采用同样预留。

无新增 RT。材质表使用五个 float4，新增记录保存 bin offset；Camera[12].xy 保存当帧向前/向后预留数量。首版仅支持普通半透明表面，不支持特殊磨砂/折射界面、VFX distortion 和局部体积；不支持的材质保留编辑值但有效 offset 为 0。

存在启用 actor 的非零有效 offset 时，全局暂停 Zero-T 硬件深度写入以及依赖该深度的 resolve closure。LUT 的虚拟排序深度不能直接写成真实 scene depth；普通 opaque 深度测试继续保留。恢复所有有效 offset 为零后自动恢复 Zero-T。Quality 面板显示暂停原因。

`avboit/tests/check_camera_offset.py` 在 2560×1440 测试 -2、0、1、2、3、4 bins，生成 `build/camera-offset-bins/comparison.png`。同一提交顺序下各偏移的 totalTau 逐字节不变，+2 bins 的 poison/dense HDR 和有效 LUT 对照一致；反转三角形提交后最大显示差异为 1/255。额外验证通过：16-bin 预算的 ±4 bins、1.5-bin 小数偏移叠加 cubic/XY min、fixed Z +4 bins，均与 dense HDR 和有效 LUT 一致；offset=0 与旧版显示逐像素一致。GPU adaptive-depth 测试覆盖 16/32/64/128 预算、adaptive/fixed 映射、前后预留和空/满 occupancy。Actor editor 回归检查实时编辑改变颜色，但不改变顶点和物理 bounds，并暂停硬件 Zero-T。

当前视角中 +1 bin 的变化较轻，+2/+3 bins 明显提高青色外壳相对内部橙色的贡献。它不是严格排序保证，仍受 splat/过滤 footprint、真实自重叠影响；adaptive 分布变化时视觉效果也可能变化。默认保留 0，按材质调整。
