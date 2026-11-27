# 烘焙烟雾资产与 gameplay mask

首版目标是单个烘焙烟雾体，密度采样乘 gameplay 提供的三维覆盖纹理。
不生成逐点实例，不恢复运行时流体模拟。动画、压缩采样、mask 与 brick 跳空已接入原型。

## 运行原型

JangaFX Q4 烟雾的低配平滑光照在 **VDB pixel march → Classified / rectangle → Brick LOD → Low / smooth lighting**，命令行参数为 `--vdb-brick-lod low`。
该档沿用 Performance 的 T=0.70 分界。每个 actor 绘制前，CS 为非空 brick 的共享角点计算带阴影的光照。细步长 draw 和 1-step draw 都从同一张 3D 贴图做硬件三线性采样。密度和 T-LUT 仍在原射线位置取样，玻璃后景仍按原规则分开累计。
128³ 资产使用 33³ 的 RGBA16F 光照缓存，纹素数据约 281 KiB。比一个 brick 更窄的光影变化可能变软或漏采。

`bin/avboit_viewer.exe --sponza --stress 24 --baked-smoke --volume-step 0.1 --volume-extinction 8`

Quality → Volumes 中开启 Baked smoke，可实时切换 Gameplay smoke mask 与 Smoke doorway open。
Scene 25 可同时检查磨砂交互。材质编辑器继续控制消光系数。
这份 mask 是 CPU 生成的 32³ R8 纹理，用于模拟 gameplay 的覆盖输入：一堵分隔墙和门洞，
关门同时清除另一侧覆盖；没有新增可见墙体，也没有实现 gameplay 连通性计算。
目前 mask 坐标随烟雾 actor，生产环境可扩展为独立的世界到 mask 变换。

当前帧来自本机 Blender 烘焙第 16 帧，转换为 Y-up。
48³ 中有 1679/1728 个非空 brick；完整二进制为 117660 B（约 115 KiB），
原 float32 密度为 442368 B。最大 voxel 密度量化绝对误差约 0.017041。
GPU 直接解码 4-bit 数据，手动 trilinear，不展开成另一张完整密度纹理。
mask 另占 32768 B；保留的旧 smoke DDS 是参考路径资源，不计入新资产压缩率。
动画与跳空现已接入，详见文末；尚无异步磁盘流送。

`python avboit/tests/check_baked_smoke.py` 检查 2K 三种覆盖状态的有限值和光学厚度单调下降。
新数据统一用于 extinction 注入、accumulation 及 MSM。没有新增渲染 pass。

## 资产来源

- [JangaFX 免费 VDB](https://jangafx.com/software/embergen/download/free-vdb-animations)：
  官方明确为 CC0。Industrial Chimney Smoke 是 250 帧、1.2 GB；High-Res Smoke
  Plume 是 167 帧、2.8 GB。这是下载包大小，不是运行时显存。
- [OpenVDB 官方样本](https://www.openvdb.org/download/)：已有 demo 使用其 smoke.vdb，
  离线转换为 64×128×64 RG16F DDS。它是单帧，不能当作烟雾动画序列。
- Blender Mantaflow 可以模拟气体并直接缓存为 OpenVDB，每帧一个文件。
  VDB 文件压缩和 GPU 采样格式需要分别考虑。

## 本机验证

本机 Blender 2.93.4 的 `build_options.fluid` 和 `build_options.openvdb` 都为 true。
`tools/bake_smoke_vdb.py` 创建单个发射源和固定 6 米立方体域。
48³、32 帧的探针烘焙生成约 20 MB 的 VDB，包含 density、flame、shadow、temperature、velocity。
独立加载第 16 帧检查到非零 density，并用 Cycles 渲染确认有可见烟雾。
验证素材、日志和预览在忽略的 `build/smoke-probe/`，不提交生成缓存。

旧版 Blender 在烘焙后直接读取 `density_grid` 时出现访问异常；脚本避免该访问。
烘焙输出完成后仍观察到 `PyEval_EvalCodeEx: NULL globals` 的退出错误，不能只用进程
退出码判断缓存是否有效。该版本目前只验证了资产生成能力，不能视为稳定自动化工具链。
正式批处理应使用经过验证的 Blender 版本，并独立验证导出的 density。

## Smolder 运行时压缩

依据 [Smolder SIGGRAPH 2026 PDF](https://www.advances.realtimerendering.com/s2026/content/AlexanderMueller_Smolder_Siggraph26.pdf)
第 58–64 页（含演讲者备注）：

- 单层稀疏 brick grid，16-bit indirection index；演讲者指出资源有触及此上限的情况。
- 每个 brick 覆盖 4×4×4 voxel。
- 每块 min/max 元数据，密度量化为 4 bits/voxel；2-bit 版本阴影细节损失较明显。
- 正方向复制一圈边界，实际数据是 5×5×5，便于在一个 brick 内完成 trilinear。
- 每 brick 一个 occupancy bit，按 4×4×2 bricks 分组，支持跳空。
- [第 60 页](https://www.advances.realtimerendering.com/s2026/content/AlexanderMueller_Smolder_Siggraph26.pdf#page=60) 给出 GI 遮蔽的粒度：每 8×8×8 个 brick 存 1 字节。第 84 页说明每个 volume 取一个 GI 采样位置，再用局部烘焙遮蔽调制。

**烘焙环境光遮蔽**。运行 `python tools/bake_smoke_ao.py <序列目录>`，从 Q4 或 FP16 烟雾生成可选的 `ambient-visibility.bin`。128³ 密度场默认对应 4³ R8 遮蔽场，每帧 64 字节。传入 `--size 16` 可提高到 16³，每帧 4 KiB。

每格在 26 个固定球面方向上求透射率，并对方向和格内位置取加权平均，得到参考可见度 `Vref`。R8 存等效密度积分 `D = -ln(Vref) / kref`，整个序列共用量化范围。默认参考值 `kref = 20.4`，对应消光系数 6/m、体积宽度 3.4 m。运行时三线性采样 D，再计算 `exp(-D × 当前消光系数 × 当前体积宽度)`。各方向光程差异越大，偏离参考消光时近似误差越明显。非等比拉伸按最长轴估计，动态 gameplay mask 的形状变化也不会触发重烘焙。

可见度只调制环境光。太阳和局部灯沿用各自的阴影。当前动画帧使用一张 R8 3D 贴图。缺少侧车文件的序列按可见度 1 渲染。可在 **Quality → Volumes → Baked smoke ambient occlusion** 切换，命令行对照使用 `--no-smoke-ao`。

**Brick LOD**。在 **VDB pixel march** 中选择 Classified 矩形或六面体后，Balanced 和 Performance 共用同一套透射率分档。精确零剔除。其余 brick 只有两档：低档在中心算光照，沿射线取一个中点样本；高档逐步采密度和光照。Balanced 的低档上限为 T = 0.50，Performance 为 0.70。跨近裁面的 brick 使用高档。[分档规则](SMOKE_PERFORMANCE.zh-CN.md#gpu-分类后的矩形与六面体2026-11-27)

上述格式不是 ZIP/Blosc VDB，也没有依据将它描述成硬件 BC4。第 64 页说明了
brick 内 trilinear filtering，但没有明确说明使用硬件 sampler 还是 shader 插值。
本 demo 的 Buffer 实现手动做三线性插值，4-bit 先加权码值再按砖的范围解码；这是本实现的选择，不能据此断言
Smolder 也使用同样的采样指令。若将量化值展开到可过滤的 3D atlas，可先硬件过滤
再用该 brick 的 min/max 还原；直接 packed 4-bit Buffer 本身不能调用硬件线性过滤。
由于边界复制和元数据，实际内存不是逻辑 voxel 数简单乘 0.5 byte；空块比例会明显影响收益。

## 接入约束

最终密度 `density = sampledBakedDensity * gameplayCoverage * extinctionScale`。
Gameplay 纹理使用独立的 world-to-volume 变换，允许与烘焙体分辨率和 bounds 不同。
相机 extinction、accumulation 和灯光 MSM 必须共用该函数，使用相同帧时间与 mask。
相机深度裁剪不能代替 gameplay 三维遮挡，后者还影响光线路径和穿墙。
动态裁掉密度后，原资产烘焙的光照/遮蔽不再严格成立，首版应保留现有动态光照与 MSM。


## 动画与跳空

32 帧、24 FPS，CPU 预载完整序列，GPU 砖缓冲按最大单帧分配。跨整数帧时上传当前帧。
extinction、accumulation 和 MSM 均采样 floor(frame)。
UI 提供播放/暂停、Restart smoke、帧位置及 Brick empty-space skipping。
`--smoke-frame 3.5` 选中帧索引 3；`--smoke-paused` 启动时暂停，headless 不推进动画。
播放到最后一帧停止，不在首尾间插值；素材尚未制作为无缝循环。

DDA 使用现有索引表，检查当前帧 brick 的占用，包含 5³ 插值支持域。
最多遍历 64 块，只跳过已证明为空的前缀，不使用密度阈值。
主视角保留原积分区间、预算、T 查询和抖动位置，只跳过密度/光照计算；
Extinction 跳过零贡献样本，MSM 可直接前移固定步长的循环计数。
因此主视角尚未消除空区间的全部循环成本。

`python avboit/tests/check_smoke_animation.py` 检查五个时间点，包括半帧和序列末尾。
2K 最终图、totalTau、三张 MSM tau 在开关跳空时完全一致。
以下跳空计时来自修正为立方体 bounds 之前的窄体积，不能代表现在更大的覆盖范围。
RTX 3070、2K、StablePowerState、60 帧中位数、半分辨率、步长 0.1m：
起始帧 accumulation 0.6610→0.3799 ms；第 16 帧 1.3286→1.3752 ms。
接近满占用时 DDA 存在额外成本；不能直接推断低端 GPU 收益。

`tools/prepare_smoke_sequence.py` 可从固定域、索引原点为零的 Blender VDB cache
复现资产，依赖离线 openvdb@0.5.1 JS reader，不是任意 VDB 的通用转换器。


## 按体素尺度采样与低透射光照

压缩烟雾 accumulation 根据当前积分段在体素空间最长轴上的跨度，选择 1、2 或 4 个
密度样本，同时考虑 gameplay mask 的分辨率。不再对很短的步长固定采四次；跨越多个
体素的大步长仍保留四次。这只是采样预算近似，不保证粗步长能重建所有细节。
原 DDS 参考体积仍保持四次采样，extinction 和 MSM 不受这个改动影响。

默认启用 **Early out below 1% local gray transmission**。纹理烟雾使用标量消光，每个积分步累加本 actor 的光学厚度。达到 4.60517 时，剩余局部透射率降至 1%，该射线停止采密度与计算光照。彩色解析体积要求三个通道都达到阈值。quad 内已结束的 lane 仍可为其他射线广播 T-LUT。T-LUT 的 RGB 权重保留了前方彩玻璃对烟雾的着色。用 `--no-volume-early-out` 可关闭尾部截断。

对继续积分的区间，若平均 RGB transmittance 的最大通道低于 0.001，保留环境光并跳过直接光照和阴影查询。此时继续累计该段的密度与光学厚度。

`--volume-reference-sampling` 供回归对照：关闭尾部截断，固定四次密度采样并保留尾部
完整光照，没有增加 ImGui 开关、RT 或 pass。
`python avboit/tests/check_smoke_sampling.py` 检查高消光、粗步长、磨砂交互，验证有限值、
相同 T-LUT，并报告透明覆盖区域的图像误差；它不能代替动画稳定性和美术验收。

RTX 3070、2560×1440、StablePowerState、60 帧中位数，固定动画 15.5 帧、
3.4m 立方体、步长 0.1m、消光 8、半分辨率、FP16 accumulation、关闭 TAA：
volume accumulation 从 6.1783 降至 4.7258 ms（约 23.5%），整帧 11.3838→10.0270 ms。
高消光 64 的覆盖区域 p99 色差为 2/255，最大 20/255；关闭 TAA 时局部噪声有所增加。
这些是同机静态样本，不代表低端显卡或运动中的最终收益。


## 默认质量配置下的步长对照

RTX 3070、2560×1440、StablePowerState、90 帧中位数，固定动画帧 15.5、
消光 8、半分辨率 volume、默认 packed accumulation、开启 TAA：

| 步长 | Volume accumulation | Upsample | TAA | 整帧 |
| --- | ---: | ---: | ---: | ---: |
| 10 cm | 4.7360 ms | 0.8376 ms | 0.3215 ms | 11.0469 ms |
| 25 cm | 2.8457 ms | 0.8366 ms | 0.3205 ms | 8.9329 ms |
| 50 cm | 1.7121 ms | 0.8371 ms | 0.3215 ms | 7.9852 ms |

50 cm 的旧四次采样/完整光照参考路径为 1.7428 ms；新增优化在这个步长下只省约
0.03 ms，不能沿用 10 cm 测试中 24% 的收益。整帧中位数包含其它 pass 的波动。
这些性能数据固定动画帧，不包含连续播放时跨帧上传的全部变化。

Upsample 并非只受 RT 格式影响：TAA 开启时使用随机扰动重建核，关闭时使用固定
bilateral 核。50 cm 独立对照：packed + 无 TAA 为 0.1812 ms；FP16 + TAA 为
0.8428 ms。之前 0.18 ms 与现在 0.84 ms 不能归因于 packed 格式。

`--volume-upsample-test --baked-smoke --smoke-frame 3.5` 以固定 60 Hz 推进烟雾动画，最后八帧移动相机，结束后恢复烟雾帧和相机。
此前 headless 测试没有推进 VDB 动画，只测试了相机运动。2K 动态截图显示：25 cm、
50 cm 在聚光灯边界的颗粒感逐渐增加，TAA 没有完全消除这个差异。默认 50 cm 未改动，
但不能将步长增大描述成无损提速。采样数下降同时减少了沿光照/阴影变化的积分点。
计时与动态对照保存在本地忽略目录 `build/smoke-50cm-taa/`。

## 高质量烟雾源对照

本地另生成了 128³、144 帧、24 FPS 的 Mantaflow 序列：持续喷射 4 秒，总长 6 秒。
64³ 基础模拟加 2 倍 Wavelet Noise，细节随流体运动输运。不是运行时流体模拟。
参考资产保留 FP16 密度（每个 brick 的 125 个值加 padding，共 256 B），用于排除
4-bit 量化影响；头部格式 3，原 4-bit 格式 1 仍兼容。该序列约 178 MiB，留在忽略的
build 目录中，没有替换仓库中的小体积探针资产。

本机复现（Blender 2.93，离线 reader 为 openvdb@0.5.1）：

```powershell
& 'C:/Program Files/Blender Foundation/Blender 2.93/blender.exe' --background --factory-startup --threads 8 --python tools/bake_smoke_vdb.py -- --output build/smoke-grenade/final128 --resolution 64 --frames 144 --noise-scale 2
.venv/Scripts/python.exe tools/prepare_smoke_sequence.py build/smoke-grenade/final128/cache/noise --pattern 'fluid_noise_*.vdb' --reader build/volume-import/openvdb.js --output build/smoke-grenade/sequence-half --size 128 --precision half
./tools/preview_smoke_grenade.ps1
```

Noise 使用 ALL cache 和 resumable bake；该版本的分阶段 bake 曾生成全空 noise grid，
转换器现在拒绝整个序列密度全零。Noise cache 读取 `density_noise`。
旧 48³ 探针可通过 `--preset probe --resolution 48 --frames 32 --noise-scale 1` 烘焙。

`--smoke-sequence PATH` 加载外部序列目录；帧滑条按实际长度调整。
预览脚本使用 2K、全分辨率 volume、2.5 cm 步长、FP16 accumulation 和完整光照采样，
起始暂停于第 79 帧。Quality → Volumes 中点 Restart smoke 可从头播放。
这是一份视觉对照配置，不是性能默认值。轮廓和动态更完整，但着色仍偏软，尚未达到最终烟雾弹效果。

## JangaFX 成品资产对照

`Smoke_Plume_01.rar` 来自 JangaFX 的 High-Res Smoke Plume 免费资产，包内授权为 CC0。
实测包含编号 0–165 的 166 个 VDB（官网标注 167 帧）。simulation_time 从 4.75 秒开始，
相邻帧相差 1/60 秒，因此按 60 FPS 播放，不宣称无缝循环，也不是从点燃开始的烟雾弹。

解压到 `build/jangafx-smoke/source` 后：

```powershell
.venv/Scripts/python.exe tools/prepare_jangafx_smoke.py build/jangafx-smoke/source/Smoke_Plume_01 --reader build/volume-import/openvdb.js --output build/jangafx-smoke/sequence128
./tools/preview_jangafx_smoke.ps1
```

导入器专用于此资产：数值排序，验证帧号、时间戳、网格变换和 bounds。原索引域
288×512×320、Y-up，填空扩展为 512³ 正方体并重采样到 128³，不拉伸形状、不逐帧归一化。
每个输出 voxel 取八个子样本平均，保存 FP16 sparse brick；这会损失原始高频细节，
不是原生分辨率参考。授权副本与来源写入转换目录。原始 VDB 和转换资产均保留在本地
忽略目录，不纳入 MIT 代码资产、不提交大文件。运行时按整数帧播放并支持 brick 跳空。

从同一 FP16 序列生成默认的 4-bit 三线性版本，保持分辨率、bounds 和帧序列不变：

```powershell
.venv/Scripts/python.exe tools/requantize_smoke_sequence.py build/jangafx-smoke/sequence128 build/jangafx-smoke/sequence128-4bit
./tools/preview_jangafx_smoke.ps1 -Precision 4bit
```

预览和场景菜单优先选择 4-bit 资产。预览脚本的 `-ReferenceSampling` 启用固定四次密度采样的对照变体。预览始终取 floor(frame)，只上传和采样当前帧。`-Precision half` 使用 FP16。预览脚本仍为高质量全分辨率配置，性能对照条件见 `SMOKE_PERFORMANCE.zh-CN.md`。

## 密度体素分辨率对比

保持 3.4 m bounds，用体素覆盖范围的密度平均生成完整低分辨率序列；不改密度倍率或动画时间。
128³、34³、14³ 分别对应 2.66 cm、10 cm、24.29 cm。最后一档作为约 25 cm 对照。
边界 brick 可以不完整，存储时补齐，采样坐标仍使用逻辑分辨率。

```powershell
.venv/Scripts/python.exe tools/downsample_smoke_sequence.py build/jangafx-smoke/sequence128 build/jangafx-smoke/sequence34 --size 34
.venv/Scripts/python.exe tools/downsample_smoke_sequence.py build/jangafx-smoke/sequence128 build/jangafx-smoke/sequence14 --size 14
./tools/preview_jangafx_smoke.ps1 -VoxelCm 10 -Frame 80
./tools/preview_jangafx_smoke.ps1 -VoxelCm 25 -Frame 80
```

默认暂停，勾选播放可检查完整动画。省略 `-VoxelCm` 使用 128³。
两份新序列是 FP16；这轮不比较 4-bit 量化。
166 帧 bin 文件总计约 181.22 / 7.17 / 1.27 MiB，不能据此推断 GPU 耗时。
2K 静态对比固定第 80 帧、2.5 cm 步长、全分辨率 accumulation、TAA、消光 6：
10 cm 保留大形状但细节明显变软，24.29 cm 丢失更多卷曲和轮廓变化。
本机截图在 `build/smoke-voxel-compare`，没有替换原始资产。

### 从场景菜单打开 JangaFX

`Scene preset → JangaFX / animated VDB smoke` 直接创建绑定动画资产的烟雾 Actor，自动播放并循环。读取 exe 所在目录上一级的 `build/jangafx-smoke/sequence128-4bit`，仅安装 FP16 时读取同级的 `sequence128`，需要先完成资产导入；不会自动下载或用测试资产替代。

在 **Actors** 选择 `JangaFX animated VDB`，可以调整 Play、Loop、Restart、Frame 和材质消光。VDB 动画是 Actor 的资产与播放状态，Scene 页不再提供切换普通烟雾/VDB 的开关。当前渲染器仍只绑定一份 VDB 序列资源，尚不支持多个独立动画资产同时播放。

## 查看 raymarch 步数与 Compute 对照

在 **Quality → Volumes** 开启 **Raymarch step heatmap**，勾选 **Show skipped steps** 可切换到跳空热图（命令行 `--volume-skip-view`）。两种视图下鼠标旁都显示两个计数，上排白色为执行步数，下排青色为跳过步数。计数器通过 `MARCH_COUNTERS` 变体编译，热图直接读取源分辨率计数。执行数扣除跳空区间；early-out 之后未遍历的尾部另行截断，两项都不计入。跳空按省去的积分区间计数。PS 的实验性 T-LUT 区间跳过若开启，也计入跳过数。同一积分步内的密度 taps、光照查询和 DDA 遍历不重复计数。半分辨率时一个计数对应屏幕上的 2×2 像素。命令行选项是 `--volume-step-view`。

JangaFX Q4 三线性场景可开启 **VDB compute wave march**（`--vdb-compute`），需开启 quad-shared T。每组处理 8×4 条射线，整 wave 无交点时提前退出，每个 2×2 quad 广播相同的 T-LUT 查询。默认逐射线计算光照。额外勾选 **Share lighting in dense smoke**（`--vdb-shared-lighting`）后，当参与着色的 lane 全部低于 0.125 透射率，完整 quad 轮换一个 lane 计算直接光照；密度和光学厚度仍逐像素积分。该近似参考 [Smolder 第 69、95 页](https://www.advances.realtimerendering.com/s2026/content/AlexanderMueller_Smolder_Siggraph26.pdf)。

CS 写入刚清空的四个 accumulation UAV，半分辨率沿用现有 upsample。当前首个可见提交的 Q4 烟雾 actor 可走 CS，其余 actor 继续用 OM 加法合成。参考采样、随机过滤、FP16 资产与性能诊断保留独立入口或编译变体。正式计时请关闭步数视图。

在 RTX 3070 上用 StablePowerState、2560×1440、半分辨率、2.5 cm 步长、JangaFX 帧 80、消光 6 做了 30 帧中位数对照。相机位于 bounds 前时，累积 PS / CS / CS 共享光照为 3.83 / 2.79 / 2.91 ms；相机进入烟中时为 6.21 / 5.44 / 5.57 ms。共享光照默认关闭。资产准备好后可运行 `python avboit/tests/check_vdb_compute.py` 检查光学厚度、最终画面和逐射线步数。

## VDB proxy stencil

半分辨率像素着色器可在 **Quality → Volumes → VDB pixel march: proxy stencil** 开启轮廓剔除（`--vdb-proxy-stencil`）。每组 2×2×2 个 brick 读取当前动画帧的索引，有密度的组把保守投影矩形写入独立的半分辨率 stencil。每个 actor 随后画一次原有的 march，保留 DDA 处理投影内的空段。两个 draw 共用 accumulation framebuffer，使用不同的 stencil ref 区分相邻 actor。镜头在浓烟内部时覆盖可能接近全屏。

同一面板的 **VDB pixel march: 14-DOP ray bounds**（`--vdb-ray-hull`）可以与 stencil 独立切换，默认关闭，适用于半分辨率 Q4 三线性、quad-shared T 的生产 PS。加载动画时从每帧已有的 brick 表生成七对平面，随当前帧上传到已有的采样常量中。PS 用交区间跳过开头和末尾的空段，保留 AABB 的步长起点、T-LUT 锚点和界面切分。烟团之间的空隙继续由 brick 遍历处理。

JangaFX 帧 80，RTX 3070，2560×1440，半分辨率、2.5 cm 步长、消光 6，开启 StablePowerState 和 stencil。两轮各 150 帧、交换 A/B 顺序后，volume accumulation 中位数在烟外近景约为 3.58 → 3.48 ms，在烟内为 7.02 → 6.84 ms，默认机位为 1.21 → 1.18 ms。

**VDB pixel march → Brick group (2x2x2)**（`--vdb-group-march`）用于对比独立分组积分。半分辨率 Q4 三线性 PS 为每个 actor 提交一次 instanced draw，非空的 2×2×2 brick 组各自投影矩形、与相机射线求交，累加到同一组 OIT MRT。开启它会替代该次绘制的 stencil 和 14-DOP。各组沿用 actor 的采样相位，每个密度 tap 由其基准体素所在的组负责，玻璃界面仍分割前后贡献。局部透射率 early-out 只在组内生效。

RTX 3070、2560×1440、半分辨率、2.5 cm 步长、StablePowerState、R11G11B10、100 帧中位数。表内是半分辨率 accumulation 的总 GPU 时间，actor 列包含 stencil 标记。稀疏对照从第 80 帧保留两团沿深度分开的烟，活跃组由 665 减至 192。

| 场景 | actor + stencil | 独立 brick 组 |
| --- | ---: | ---: |
| 烟外近景，消光 6 | 3.58 ms | 4.52 ms |
| 烟内，消光 6 | 7.02 ms | 9.93 ms |
| 两团稀疏烟，消光 6 | 1.67 ms | 1.76 ms |
| 烟外近景，消光 32 | 2.62 ms | 4.07 ms |
| 同一高消光近景，关闭 early-out | 3.06 ms | 4.08 ms |

关闭 early-out 的玻璃穿烟对照，RGBA16F 累积的最终图最大差 1 个 8-bit 色阶，R11G11B10 为 4 个色阶。分组路径会多次向目标格式舍入。独立组默认关闭。准备好 JangaFX 资产后，可运行 `python avboit/tests/check_vdb_groups.py` 重跑画质回归。

### 单个物理 brick

在 **VDB pixel march** 下选择 **Single brick / per-step lighting**（`--vdb-brick-march`）或 **Single brick / per-brick lighting**（`--vdb-brick-coarse-light`）。每个 actor 一次 instanced draw。VS 剔除空 brick，并传入当前帧的 Q4 record 地址和解码范围。PS 裁剪当前 4³ brick 和可见深度，直接读取带 apron 的数据。整条射线共享等距采样和 jitter；间距取设置的步长与沿射线最快轴跨过一个体素的距离两者中的较小值。可勾选 **Double brick ray step**（`--vdb-brick-double-step`）把这个间距加倍，最快轴上限随之变为两个体素。默认使用原步长。这个 PS 独立于 actor 的步数预算、自适应步长和 quad T-LUT。Early-out 作用于单个 brick。

Q4 单 brick 的各个光照模式均可在 ImGui 切换 **Stochastic smoke filtering (experimental)**，或传 `--smoke-stochastic-filter`。像素 shader 用体素坐标的小数部分为三个轴分别选择邻点，每步读取一个 Q4 密度，随机种子包含像素、时间采样相位和射线的全局步号。默认三线性与随机过滤独立编译。Extinction splat 和光源阴影始终按原密度过滤。

逐步模式按非零密度采样点查询 T-LUT 和光照。逐 brick 模式沿用相同的密度样本，在玻璃前后分别累计光学厚度，在每侧的密度加权平均深度查询一次 T-LUT 和光照。强光影过渡处可能出现沿 brick 边界的条带。

**Single brick / vertex lighting**（`--vdb-brick-vertex-light`）在 VS 对 4³ brick 的八个空间角点求光照。相邻 brick 的公共面使用同一组角点位置。PS 按射线样本的体素坐标三线性插值照明，逐步读取 T-LUT，保留玻璃前后分账。硬阴影会在约一个 brick 的尺度上变软。默认绘制 indexed quad，两个三角形共享首顶点。首顶点计算八个角点的光照，通过普通 `nointerpolation` varyings 传到 PS。可用 `--no-vdb-brick-indices` 对比非索引绘制。各像素在 `ClipBrick` 中裁剪射线和当前砖的交区间。

**Single brick / cached lighting**（`--vdb-brick-cache`）在每个 VDB draw 前用 CS 更新共享角点的 RGBA16F 3D 贴图。JangaFX 第 80 帧有 4,279 个含 apron 的非空 brick，共用 5,618 个角点。CS 在 33³ 格点中检查相邻 brick，只对用到的角点求光照。缓存随 actor 顺序复用，角点光照每帧重算。PS 在采样点对共享角点贴图做硬件三线性过滤。

关闭 **Half-resolution local volumes** 或传入 `--volume-full-res`，即可让三个单 brick 模式按内部渲染分辨率光栅化。PS 按实际像素读取场景深度与界面深度，直接写全分辨率 accumulation MRT。GPU 计时在全分辨率时计入 `accumulation_ms`；半分辨率时烟雾绘制和上采样分别计入 `volume_accumulation_ms`、`volume_upsample_ms`。比较两种分辨率时可逐帧相加这三项。

同上测试条件，RTX 3070 的半分辨率 accumulation 中位数如下。程序默认使用 actor 路线。

| 场景 | actor + stencil | 单 brick 逐步着色 | 单 brick 合并着色 |
| --- | ---: | ---: | ---: |
| 烟外近景，消光 6 | 3.58 ms | 2.29 ms | 1.20 ms |
| 烟内，消光 6 | 7.02 ms | 5.82 ms | 3.16 ms |
| 两团稀疏烟，消光 6 | 1.67 ms | 0.77 ms | 0.41 ms |
| 烟外近景，消光 32 | 2.62 ms | 2.03 ms | 1.10 ms |

同一硬件、JangaFX 第 80 帧和 2.5 cm 步长下，比较首顶点串行计算与三顶点分摊。下面是逐帧合计上述三个 accumulation / upsample 计时后的 100 帧中位数，玻璃场景包含玻璃本身的 accumulation。

| 镜头 | 1280×720 串行 | 1280×720 分摊 | 2560×1440 串行 | 2560×1440 分摊 |
| --- | ---: | ---: | ---: | ---: |
| 烟外近景 | 1.86 ms | 1.69 ms | 5.02 ms | 4.59 ms |
| 烟内 | 4.17 ms | 3.82 ms | 13.95 ms | 12.93 ms |
| 玻璃穿烟 | 2.10 ms | 1.93 ms | 5.22 ms | 4.80 ms |

运行 `python avboit/tests/check_vdb_brick.py` 验证常密度、部分边缘 brick、Q4 编码、空帧和玻璃前后分账。

## 近景 VDB 射线 LOD

选择 **JangaFX / animated VDB smoke**，在 **Quality → Volumes** 开启 **VDB compute wave march** 和 **Wave ray LOD: quad / diagonals / pixel**。默认使用独立射线。命令行为 `--vdb-compute --vdb-near-ray-lod`，使用 Q4 三线性和四步 T-LUT。

每四个深度步检查一次当前块末端的投影间距。同一 wave 的活跃 quad 统一选档，任一 quad 超出当前档的容差，整个 wave 从这个块起切到下一档。容差按各像素射线到其代表射线的距离计算，单位是 VDB 体素。近端容差为 0.20，每个 2×2 quad 沿中心射线并行求四步。中档容差为 0.55，左上与右下、右上与左下各走一条射线，各自并行求两步。两条对角线的中点重合，因此代表位置从中心朝各自一个端点移半程，相邻 quad 交替选端点。超过中档容差后，每个像素沿自己的射线继续积分。

每次切档保留已经累计的颜色、权重、光学厚度和玻璃后的贡献，从首个未处理的深度块继续。共享阶段要求四条射线都有交点，入口和可见出口的深度差均在四分之一步内，且整段位于玻璃界面同侧。遇到界面穿插或 Zero-T 截断等不连续情况，参与判定的 wave 走独立射线。步数调试导出的 `native-march-counters.rgba32f` 的 A 通道按位记录该像素实际经过的档位，4 为 quad，2 为对角线，1 为独立射线，7 表示同一像素经过三档。

`--vdb-compute --vdb-quarter-ray-reference` 提供固定低分辨率对照：在 720p 半分辨率模式中，用 320×180 条独立射线填充同一组 640×360 累积贴图，继续读取原始 720p 深度、界面和 T-LUT，复用原 upsample。运行 `python avboit/tests/check_vdb_near_ray.py` 比较贴脸、烟内、浓烟、远景和玻璃穿烟的 GPU 时间与画面误差。
