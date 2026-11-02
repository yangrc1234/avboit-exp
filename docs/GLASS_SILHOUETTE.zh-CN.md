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

## XY cubic 对比

Quality → OIT sampling → `Cubic T-LUT (XY)` 可以实时切换，或使用 `--lut-cubic`。这个开关与 frost 的 bicubic 重建独立，不增加 RT。默认仍为 linear。

实验采用正权重 cubic B-spline，以四次硬件 trilinear 采样重建 XY；Z 仍为 linear，保留原来的两 slice surface bias。它只验证 XY 滤波影响，并非完整复现论文的 cubic 深度重建。Accumulation、Resolve、体积与 VFX 使用同一采样入口；同时扩大斜率估计和 Zero-T 的保守查询范围，以覆盖新的 XY footprint。

在上述测试中，cubic 柔化了台阶，但亮边扩散得更宽：边缘平均 HDR 误差从 0.0111 增至 0.0186，99 分位误差从 0.0981 降至 0.0919。两种模式的 LUT 内容完全一致，差别来自采样。Cubic 无法恢复低分辨率光栅已经丢失的覆盖信息，因此不能单独作为这个问题的修复。

诊断脚本另外输出 `cubic-comparison.png`（linear / cubic / 精确排序参考）和 `cubic-motion.gif`。Zero-T、磨砂及实时切换回归测试均通过。

RTX 3070、2560×1440、StablePowerState 开启，按 linear / cubic / cubic / linear 顺序，每次预热 30 帧、记录 120 帧，以每次中位数比较：

| 场景 | Linear 整帧 GPU | Cubic 整帧 GPU |
| --- | ---: | ---: |
| 27：玻璃轮廓 | 1.25–1.26 ms | 1.29–1.30 ms |
| 10：磨砂测试 | 0.81–0.82 ms | 0.86 ms |

这里使用默认 packed 格式测性能。数据只代表这两个固定视角，不代表复杂场景或低端 GPU 的成本比例。

## Conservative splat 实验

Quality → OIT sampling → `Conservative surface splat (experimental)`，或 `--conservative-splat`，只改变表面 extinction 的硬件光栅覆盖，accumulation 和体积 raymarch proxy 保持普通光栅。默认关闭；不支持该硬件能力时 UI 禁用，显式命令行请求会报错。不增加 RT，实时切换 PSO 并重置 TAA history。

实验开启时，只有 extinction occupancy 的 XY 范围增加一个低分辨率 texel 的安全边界，覆盖保守光栅的不确定区域；ScreenTiles、frost 和普通模式不变。此模式仍是质量诊断，不是生产用修复。

场景 28（`Glass silhouette / three layers`）在场景 27 的后层后面增加一层蓝色玻璃，可检查前层外侧仍有两层透明时的影响。运行 `avboit/tests/check_conservative_splat.py`，会在 `build/conservative-splat` 生成对照图和 JSON 指标。两种模式都使用 linear T-LUT，2560×1440，FP16 accumulation/composition。

| 场景 27 区域 | 普通光栅平均 HDR 误差 | 保守光栅平均 HDR 误差 |
| --- | ---: | ---: |
| 斜边 | 0.00840 | 0.00149 |
| 带圆孔玻璃的边缘区域 | 0.00534 | 0.00617 |
| 细条 | 0.02858 | 0.02580 |
| 玻璃内部 | 0.00047 | 0.00177 |

几何斜边明显改善，但共享三角形边出现暗线：相邻三角形对同一个 LUT texel 各执行一次 atomic add，把同一表面的消光累加两次。材质裁剪的圆孔仍是点采样，硬件保守覆盖不能补足这个轮廓。三层场景内部和前层外侧也有退化。场景 17、27、28 的 poison-extinction 与 dense-extinction 对照均得到完全相同的 HDR 图像，因此这些场景中的暗线不是稀疏清理遗漏。

D3D12 的 ConservativeRaster 只有 ON/OFF。Tier 3 的 SV_InnerCoverage 是“像素是否完全位于当前三角形内部”，不是覆盖率或共享边归属；不能直接修复上述重复累加。见 [D3D12 conservative rasterization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/conservative-rasterization)。

## 积分后的 T-LUT XY min-filter

Quality → OIT sampling → `T-LUT XY min-filter (experimental)`，或 `--lut-min-filter`。默认关闭。对比时保持普通 splat、linear T-LUT 查询，不同时开启前两个实验。

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
| 26 noise 外壳与内层 | 青色外壳和内部橙色的相对权重明显变化，局部条纹变化；约 3.77% 全屏像素超过 2/255，仍不适合直接全局默认开启 |
| 0–2、4–18 其余玻璃、磨砂、折射、浓烟 | 当前视角变化大多局限于局部边缘；场景 14 烟雾有轻微变化 |
| 19–25 均匀体积、重叠体积、OpenVDB | 当前视角差异较小，未见明显新增块状瑕疵 |

31 个场景的 HDR 均为有限数，LUT 沿 Z 单调。另记录到一个尚未定位的既有一致性问题：场景 6/14/26 中，有些柱子的量化 LUT 已全 RGB 为零，而 ZeroSlice 未指向捕获 LUT 的首个零 slice；OFF 就存在，ON 下对应柱数从 58/1034/64 变为 70/1075/114。不能将这些场景声明为 Zero-T 元数据完全一致，后续需要单独检查量化阈值与 metadata 生成。此轮没有为掩盖该问题修改比较条件或渲染算法，报告保留了统计。
