# 前层玻璃轮廓覆盖后层玻璃

Test case 选择 `Glass silhouette over rear glass / XY aliasing`，或运行 `bin/avboit_viewer.exe --stress 27`。

后面是一整块有色玻璃，前面依次是斜边玻璃、带圆形开孔的玻璃、三条不同宽度的细玻璃。所有轮廓都落在后层玻璃内部。各层是独立 actor/material/draw，可直接编辑。勾选 `Animate` 可让前层缓慢平移，观察轮廓色带随低分辨率 texel 覆盖变化；也可以手动移动相机。

默认两层深度相隔 2.2 m，表面垂直相机，材质均匀，frost 和 distortion 为零。这样先隔离 XY 覆盖问题，不混入近距离 Z 分层、折射、磨砂或材质 noise。

## 复现结果

2560×1440 下，前层轮廓内侧出现发亮的台阶色带，细条也有不均匀透射。1/8 XY extinction raster 没有与全分辨率覆盖完全一致；后层片元查询 bilinear T-LUT 时，会混入前层没有覆盖的邻近 texel，得到偏高的前方透射率。Resolve 的 totalTau 能保持总透射，但无法把两个表面的颜色权重重新分配正确。

运行离线诊断：

```powershell
.venv/Scripts/python.exe avboit/tests/check_glass_silhouette.py
```

输出位于 `build/glass-silhouette`：完整画面、局部三栏对比、轻微平移动图和 JSON 指标。参考图按逐像素真实覆盖执行前后层 RGB over；它保留单样本光栅边缘，不是抗锯齿参考。测量排除紧贴轮廓的一个像素，以区分几何本身的锯齿与 LUT 产生的额外色带。诊断使用 FP16 accumulation/composition 排除 packed 格式误差；不用于测性能。

初次验证中，默认 1/8 LUT 的边缘平均 HDR 最大通道误差约 0.0111，1/2 LUT 为 0.0010；两者内部误差均约 0.00047。关闭 Zero-T、关闭 slope splat、反转提交顺序均不改变该测试的 HDR 图像。移动前层后误差随覆盖相位变化。提高 LUT 分辨率只是定位手段，本次没有改变默认质量，也没有实现修复。

## 已收敛的对照方案

Cubic T-LUT 与硬件 Conservative splat 的 UI、命令行和实现已移除；当前统一使用普通光栅和硬件线性 T-LUT 查询。Frost 的 bicubic 重建不受影响。

历史测试中，Cubic 柔化了台阶，但边缘平均 HDR 误差由 .0111 增到 .0186；硬件 conservative raster 改善了部分几何轮廓，却因共享三角形边重复累加 extinction 引入暗线，且不能补足材质裁剪轮廓。因此保留积分后有下限保护的 XY min-filter。

## 积分后的 T-LUT XY min-filter

Quality → OIT sampling → `T-LUT XY min-filter`，默认开启；命令行 `--no-lut-min-filter` 可关闭做对比。使用普通 splat 和 linear T-LUT 查询。下列早期对比数据保留当时的测试条件。

每根柱先独立积分，随后在同一 Z slice 对邻近 3×3 柱的 RGB T 分别取 min。不得把该 min 结果反馈到下一 slice 的积分中，也不对原始 extinction 做 max，否则倾斜表面可能被累加成厚层。

实现融合在 integration：10×10 线程组积分 8×8 输出柱和一圈 halo；每个 slice 通过 groupshared 交换 T，只有中央 8×8 写最终 LUT。没有新 RT 或 pass，查询端仍为一次硬件线性采样。代价是 halo 的重复积分、每 slice 两次组内同步，以及 min 运算。各源柱按自己的 physical occupancy 读取，未占用数据不会参与；不需要扩大原始 extinction occupancy。

ZeroSlice 和 Zero-T candidate 根据最终过滤后的 T 生成，避免深度剔除与消费者使用不同的 LUT。斜率 splat 的 footprint 增加一个 LUT texel，以包含 min-filter 的半径，但保留现有最大 Z push 限制。极端斜面、细缝、相邻不同颜色介质仍可能有遮挡外扩误差；本实验不保证任意场景无自遮挡。

运行 `avboit/tests/check_lut_min_filter.py`，输出到 `build/lut-min-filter`。2560×1440、FP16 诊断结果：

| 区域 | 普通 integration 平均 HDR 误差 | XY min-filter |
| --- | ---: | ---: |
| 两层玻璃边缘 | 0.01113 | 0.00065 |
| 三层玻璃边缘 | 0.01202 | 0.00086 |
| 两层玻璃内部 | 0.00047 | 0.00047 |
| 交叉斜面内部，正面视角 | 0.00273 | 0.00280 |

三个额外斜视角（camera preset 7/8/9）内部平均误差与原版差异均小于 0.00003。平面案例 GPU LUT 与 CPU 3×3 min 参考逐字节一致；场景 11/13/17/27/28 的 poison、dense 对照均一致，输出沿 Z 保持单调。实时切换不重建 LUT，actor editor 回归测试覆盖了这一点。

场景 27 默认 packed 格式，RTX 3070、2K、StablePowerState，ordinary/min/min/ordinary 各预热 30 帧、测量 120 帧：integration 中位数 0.146→0.243 ms，整帧约 1.26→1.35 ms。此数据仅代表这个固定场景。

## 全场景检查与八层玻璃

运行 `avboit/tests/compare_lut_min_scenes.py`，覆盖 0–30 共 31 个场景，输出 `build/lut-min-scenes/index.html`。每个场景有 OFF/ON 的 2K 原图、变化区域裁剪和放大差分。使用默认 packed accumulation、普通 splat、linear T-LUT，固定视角、未开启 TAA；差分数值表示变化，不等同于画质误差，也不代表运动稳定性。

新增场景 29 为八层 RGB 透射不同、轮廓错开的玻璃，30 为同一布局但最近一层增加 frost/refraction。各层仍是独立 actor/material/draw。在当前固定视角，两者均未看到随层数增加而明显变暗或出现三角形接缝；场景 29 与精确排序参考比较，八层完全重叠区域的显示域最大通道平均误差，OFF/ON 均约 1/255。此测试使用均匀背景，场景 30 只能作为多层兼容性检查，不能充分评价磨砂细节。

| 场景类别 | 本轮观察 |
| --- | --- |
| 27/28 两层、三层玻璃轮廓 | 改善明显；有精确排序参考支持 |
| 29/30 八层 RGB、八层加前层磨砂折射 | 开关变化较小，未见明显新增接缝或整体变暗 |
| 3 细条与烟雾 | 细条更清晰，约 8.01% 全屏像素变化超过 2/255；缺少参考，不能把全部变化视为改善 |
| 26 noise 外壳与内层 | 青色外壳和内部橙色的相对权重明显变化，局部条纹变化；约 3.77% 全屏像素超过 2/255，当时仍不建议直接全局默认开启（早期结果，后续加入下述零透射保护） |
| 0–2、4–18 其余玻璃、磨砂、折射、浓烟 | 当前视角变化大多局限于局部边缘；场景 14 烟雾有轻微变化 |
| 19–25 均匀体积、重叠体积、OpenVDB | 当前视角差异较小，未见明显新增块状瑕疵 |

31 个场景的 HDR 均为有限数，LUT 沿 Z 单调。另记录到一个尚未定位的既有一致性问题：场景 6/14/26 中，有些柱子的量化 LUT 已全 RGB 为零，而 ZeroSlice 未指向捕获 LUT 的首个零 slice；OFF 就存在，ON 下对应柱数从 58/1034/64 变为 70/1075/114。不能将这些场景声明为 Zero-T 元数据完全一致，后续需要单独检查量化阈值与 metadata 生成。此轮没有为掩盖该问题修改比较条件或渲染算法，报告保留了统计。

## 零透射邻居扩散保护

纯 XY min 会把邻居的零 T 扩散到中心，导致 accumulation 分子/分母归零；即便关闭 Zero-T depth draw 和 shading shortcut 仍会发生。场景31专门覆盖两个不透明前层的斜边及单独红通道饱和，后面是高 opacity 的明亮半透明面。

现在逐通道使用 `min(centerT, max(neighbourMinT, 16/255))`。中心自己的零值和低透射率保持原样，只限制邻居引起的额外衰减。下限为16个RGBA8码值，是兼顾R11G11B10 accumulation的实验选择，不是任意opacity/颜色下的数值保证。直接排除零值邻居会在邻居从正数变为零时改变参与集合，可能使T随Z上升；有下限的min则保持Z单调。无新增RT/pass或额外查询。

本次2K验证关闭硬件Zero-T和shading shortcut：旧min在场景26/31分别新增1803/13112个有消光但零分母的FP16颜色通道。新处理消除了这两个测试里的FP16零分母。场景26 packed accumulation相对不做min的基线，1/255、4/255、16/255分别新增12、5、3个零分母通道；很小opacity/颜色仍可能受格式量化影响，不能宣称完全消除所有数值问题。

前面纯min的画质/计时数据属于历史实验；当前回归脚本使用带16/255邻居下限的参考。过滤当前默认开启。


## 内外壳压平排序实验

场景 32（Nested cubes）包含三组 0/30/60 度倾角的立方体，外壳与内层相差 1 cm，分别是独立 actor/material/draw。场景顶部的 `Flatten nested cube OIT ordering` 开关可对比共享锚点压平与普通外壳 +1 bin camera offset。Actor 材质面板也可以选择 `OIT depth anchor` 和 `OIT flattened layer`；内外层必须指向同一个 actor 锚点。

锚点投影为视空间深度，经 Adaptive Z 映射并取整得到 k。内层仅写 k，外壳仅写 k−1；查询 inclusive T-LUT 时分别读 k−1、k−2 的 bin 中心，排除自身。该模式绕过 slope push 和普通 camera offset；真实顶点、opaque depth test、着色和 totalTau 不变。Occupancy 使用真实 XY bounds 和锚点附近的虚拟 Z；复用现有向前一个 physical bin 的保留逻辑。没有额外 RT/pass，材质 GPU 记录大小也未增加。

2K、无 TAA、FP16 accumulation/composition 验证：正面捕获只有两个相邻的 LUT 跃迁（4、5）；关闭 slope、16-bin budget、poison 和 dense 对照的 HDR 逐值相同。逆序提交只有 FP16 混合舍入差异（该捕获最大 HDR 差约 0.00147）。普通 +1 bin 模式可见的斜面周期条纹，在当前压平对照中消失；另检查了三个侧移／环绕视角，低分辨率 XY 覆盖和 noise 边缘误差仍然存在。

这只提供层级排序近似：同层内部的真实前后关系被丢弃，与其他模型的穿插仍按共享 LUT 的虚拟深度处理。Frost/refraction interface 和 local volume 不启用此模式；沿用 camera offset 的限制，启用时暂停硬件 Zero-T 深度剔除。测试脚本为 `avboit/tests/check_flat_ordering.py`，图片位于 `build/flat-ordering`。


## 第三层轮廓黑边复现

场景 33（Third-layer halo）专门测试之前未覆盖的布局：前层是整片较暗的玻璃，中间是低透射率的斜边和孔洞，第三层是明亮的半透明面。三层彼此分离、平行于相机；没有 camera offset、frost/refraction 或零透射率材质。

中间层真实轮廓外的像素只覆盖第一、第三层，但低分辨率 LUT 的线性采样和 XY min-filter 会把中间层消光带入该像素。第一层的 T 查询在中间层之前，保持不变；第三层的权重下降，归一化不能同时恢复两层正确的相对贡献，因此产生暗轮廓。16/255 邻居下限可以避免传播零值，却不能消除非零 T 的错误衰减。关闭 min 仍有普通低分辨率覆盖/双线性插值的残余；不能将 min-filter 视为多层轮廓问题的完整修复。

`avboit/tests/check_third_layer_halo.py` 生成精确逐像素排序参考与对照图。当前 2560×1440、无 TAA 的固定视角，在中间层轮廓外 2–16 像素的区域统计，最大 RGB 通道平均 HDR 变暗量为：默认 1/8 LUT + min 约 0.213、关闭 min 约 0.016、提高到 1/2 LUT + min 约 0.037。这里是特定高反差场景的误差，不是通用画质分数。

关闭硬件 Zero-T 和 shading shortcut，捕获 HDR 逐值不变。改 FP16 accumulation 后黑边依旧（同一测量约 0.213）；原 packed 格式逆序提交还存在混合舍入差异，但不是主要暗轮廓的来源。本次仅加入复现场景和诊断，没有为了该案例修改默认滤波策略。


## 限制 min-filter 的额外消光

Quality → OIT sampling → `Min-filter retain` 可以实时调整，试验默认值 0.5。计算仍在同一个 integration 中完成：`min(centerT, max(neighbourMinT, max(16/255, centerT * retain)))`。centerT 始终是当前柱独立积分的原值，不是遍历邻域时不断变小的结果。原有 16/255 零扩散保护保留；当前柱自身的零或低 T 不被抬高。无新增 RT、pass 或纹理采样，改动是寄存器内的相对下限计算。

retain=0 对应之前仅有绝对下限的 min；0.5 限制邻居最多把当前柱的 T 再减半；1 不改变中心 T。retain=1 用于画面对照，仍执行 min 的线程组和同步；性能上不等价于关闭 min-filter。

`avboit/tests/check_relative_lut_min.py` 的 2560×1440、无 TAA、FP16 诊断结果（与精确排序参考的平均最大 RGB 通道 HDR 误差）：

| 测量区域 | 旧 min | retain=0.25 | retain=0.5 | 关闭 min |
| --- | ---: | ---: | ---: | ---: |
| 场景 27：前层轮廓内侧 | 0.00065 | 0.00276 | 0.00563 | 0.01113 |
| 场景 33：中间层轮廓外侧 | 0.21267 | 0.15033 | 0.09063 | 0.01596 |

这是抑制黑边与背景漏色之间的折中，不是恢复真实覆盖。两类 ROI 不同，不能横向比较绝对数值。当前测试 retain=1 与关闭 min 的 HDR 完全一致；0.5 的 sparse/poison/dense 结果一致，场景 27/33/31 未新增零 T，LUT 沿 Z 保持单调。更精确地恢复遮挡仍然需要额外信息。本轮没有加入预先计算 totalTau 的绘制。
