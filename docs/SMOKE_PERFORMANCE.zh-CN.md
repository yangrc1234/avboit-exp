# JangaFX 近距离性能审计（2026-11-06）

## Coherent upsample 收敛

当前只保留 coherent 四邻居核，TAA 关闭时自动关闭中心抖动。删除旧随机核、
9-tap reference、ImGui 选择框和 CLI 核选择参数。
同一 2K / StablePowerState / 近距离相机 10 / 4-bit 单帧 / 10 cm / 半分辨率配置，
60 帧中位数：upsample 从旧核 1.3445 ms 降至 0.3717 ms（约 72%）；
volume accumulation 为 5.0181 ms，与之前 5.0202 ms 基本相同。
静止／移动镜头 TAA 捕获及无 TAA 路径通过有限值检查。
数据：`build/coherent-upsample-check/`。

以下旧数据采集时使用旧随机 upsample，保留作为历史对照。

## 4-bit 与单帧采样对照

RTX 3070，2560×1440，StablePowerState，30 帧预热、60 帧采样中位数。
同一份 JangaFX 128³ 序列，半分辨率 volume、10 cm 步长、TAA，extinction=6。
双帧在 60.5 帧混合 60/61；单帧取 floor(frame)，实际只上传、读取第 60 帧。
这里的单帧是取消时间插值，仍支持序列播放。

| 密度格式 / 时间采样 | 贴近烟体 accumulation | 烟体内部 accumulation |
|---|---:|---:|
| FP16 / 双帧 | 6.624 ms | 8.856 ms |
| FP16 / 单帧 | 5.134 ms | 6.599 ms |
| 4-bit / 双帧 | 6.399 ms | 8.344 ms |
| 4-bit / 单帧 | 5.020 ms | 6.418 ms |

贴近视角逆序复测依次为 FP16 双帧 6.595、FP16 单帧 5.128、4-bit 双帧 6.414、
4-bit 单帧 5.021 ms。单帧节省约 22–26%；4-bit 在相同时间采样模式下省约 2–6%。
贴近视角 upsample 约 1.345 ms，四组基本不变，不能将上述 accumulation 当作烟雾总开销。

166 帧文件总量从 181.22 MiB 降为 55.76 MiB（减少 69.2%）。当前 GPU buffer 容量：
FP16 双帧 / 单帧为 2.296 / 1.148 MiB；4-bit 双帧 / 单帧为 0.702 / 0.351 MiB。
完整序列仍驻留 CPU，尚未改成全部帧 GPU 常驻。两种编码都保留手工空间 trilinear，
4-bit 并未引入硬件过滤；压缩收益不能直接等同于着色提速。

量化不改变网格和 bounds。相同时间模式下比较烟雾覆盖像素的最终 8-bit 画面，
99% 通道误差不超过 1/255，最大 8/255。原始 accumulation 无非有限值或负值。
FP16 单帧 60.5 与原双帧模式整数帧 60 的画面、totalTau 和 LUT 逐字节一致。
本轮固定时间测量不含连续播放的跨帧上传，也未验证取消插值后的动态跳帧感。
目前仍达不到近距离 1.5 ms 目标。

复现脚本：`avboit/tests/check_smoke_formats.py`；数据与截图：
`build/smoke-format-profile/`（`summary.json`、`quality.json`、各配置 CSV）。

## 先前步长与采样审计

RTX 3070，2560×1440，StablePowerState，30 帧预热、45 帧采样中位数。
Smoke_Plume_01 128³ FP16，第 60.5 帧，extinction=6，太阳强度 6；TAA 开启。
相机 10 位于 (0,1.75,2.65)，朝 +Z；相机 11 位于烟体内部 (0,1.75,4)。
现有画质预览没有修改。新增 10/11 两个相机预设仅用于复现近距离测试。

| 配置（相机 10） | Accumulation ms | Upsample ms |
|---|---:|---:|
| 全分辨率，2.5 cm，参考采样 | 85.956 | 0 |
| 半分辨率，2.5 cm，参考采样 | 19.910 | 1.680 |
| 半分辨率，2.5 cm，自动采样数 | 12.950 | 1.682 |
| 半分辨率，10 cm，自动采样数 | 6.590 | 1.668 |
| 上项关闭体积阴影 | 6.284 | 1.609 |
| 上项保留阴影、固定 bilateral 升采样 | 6.555 | 0.287 |
| 半分辨率，50 cm，固定 bilateral | 2.564 | 0.287 |

全分辨率的 volume 工作计入 accumulation_ms；半分辨率才计入 volume_accumulation_ms。
内部视角在半分辨率 10 cm 下为 9.279 ms accumulation + 1.681 ms upsample。
关闭 brick skipping 时 accumulation=10.129 ms；保持跳空、整数帧时为 5.466 ms。
Shadow stage 包含普通阴影和 MSM，不能全部算作烟雾新增开销。
近距离 extinction clear 约 0.77 ms，splat 0.24 ms，integration 0.51 ms，属于共享 AVBOIT 路径。

## 判断

首要瓶颈是大量 ray steps 中的双帧密度读取、手工 trilinear 解码、T-LUT 查询与控制流。
参考路径每步四次密度；每次双帧各解码八个 voxel，最多 64 个 voxel 值，不等于 64 次 DRAM 事务。
跳空保留；它当前跳过密度/光照但仍迭代原步进格点并查询 T-LUT。
仅降阴影质量收益有限。随机四 tap 升采样存在大量独立寻址、导向深度和四张 MRT 的点加载。

首选实验：每帧解码并预插值到一张 128³ R16F（4 MiB）纹理，统一供 splat、raymarch、MSM 使用。
让 GPU 硬件做空间插值；时间插值在线性密度域完成，避免逐像素重复双帧解码。
新增构建 pass 与资源状态转换必须一起计时，不能将其开销移出预算。
其次比较固定或 coherent 四 tap 重建，保持 TAA 并做运动验收；再评估 5–10 cm 的步长。
仍不足时再考虑更低分辨率，但不保证视觉无损。

本轮是固定半帧测试，包含双帧采样，不含每帧切换时完整资产上传成本。
当前实现跨帧会重传两个 brick 帧；后续应保留上一帧，仅上传新帧，并单独测动态播放。
测试只验证静态画面与耗时，不能据此认定放大步长或换重建核后仍保持认可的动态质量。
1.5 ms 若包含全部烟雾增量，必须另做无烟基线；本轮共享 pass 耗时不能直接当增量相加。

原始 CSV、command.json 和截图：`build/jangafx-profile/`；汇总：`summary.json`。
