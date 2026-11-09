# 烘焙烟雾资产与 gameplay mask

首版目标是单个烘焙烟雾体，密度采样乘 gameplay 提供的三维覆盖纹理。
不生成逐点实例，不恢复运行时流体模拟。动画、压缩采样、mask 与 brick 跳空已接入原型。

## 运行原型

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
- 另有极低分辨率的烘焙 GI occlusion 数据。

上述格式不是 ZIP/Blosc VDB，也没有依据将它描述成硬件 BC4。第 64 页说明了
brick 内 trilinear filtering，但没有明确说明使用硬件 sampler 还是 shader 插值。
本 demo 的 Buffer 实现逐体素解码后手动插值；这是本实现的选择，不能据此断言
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

32 帧、24 FPS；全序列 2749200 B（2.62 MiB），CPU 预载；GPU 双帧缓冲
最大 241984 B（236 KiB）。跨帧上传两个帧，帧内更新 32 B 头部。
密度在相邻两帧间插值，extinction、accumulation 和 MSM 使用相同帧时间。
UI 提供播放/暂停、Restart smoke、帧位置及 Brick empty-space skipping。
`--smoke-frame 3.5` 指定初始半帧状态；`--smoke-paused` 启动时暂停，headless 不推进动画。
播放到最后一帧停止，不在首尾间插值；素材尚未制作为无缝循环。

DDA 使用现有索引表，检查两帧 brick 的占用并集，包含 5³ 插值支持域。
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

当积分段两端平均 RGB transmittance 的最大通道低于 0.001 时，保留环境光，跳过直接
光照和阴影查询。密度、totalTau、denominator 与 BackN 的累计继续进行，不因低 T
直接丢弃尾部。阈值没有考虑灯光强度，极亮灯光下可能损失仍然可见的尾部贡献；这也
不是 Smolder 的 wave 共享光照方案。

默认启用。`--volume-reference-sampling` 仅供回归对照：固定四次密度采样并保留尾部
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

`--volume-upsample-test --baked-smoke --smoke-frame 3.5` 现在会在两组重建核测试中
以固定 60 Hz 推进同一段烟雾动画，最后八帧移动相机，结束后恢复烟雾帧和相机。
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
忽略目录，不纳入 MIT 代码资产、不提交大文件。运行时保持已有双帧插值与 brick 跳空。

可从同一 FP16 序列生成 4-bit 对照，保持分辨率、bounds 和帧序列不变：

```powershell
.venv/Scripts/python.exe tools/requantize_smoke_sequence.py build/jangafx-smoke/sequence128 build/jangafx-smoke/sequence128-4bit
./tools/preview_jangafx_smoke.ps1 -Precision 4bit -SingleFrame
```

`-SingleFrame` 对应 viewer 的 `--smoke-single-frame`：取 floor(frame)，取消双帧时间
插值，只上传和采样当前帧，仍可播放动画。省略该参数保留双帧；`-Precision half`
使用 FP16。预览脚本仍为高质量全分辨率配置，性能对照条件见 `SMOKE_PERFORMANCE.zh-CN.md`。
