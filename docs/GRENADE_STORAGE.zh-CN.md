# Gameplay smoke GPU 存储对比

2026-11-16，RTX 3070 实测。以下是径向减法侵蚀引入前的密度模型与数据量；2026-11-17 GPU header 增加了 4 B 半径参数，密度模型变化后的性能需要重新测量。当前默认保持 **Dense R8、不启用 brick skipping**；三个 GPU 路径可在 ImGui 的 Volumes → GPU coverage storage 切换。

## 实现

覆盖范围为 8 m 立方体，10 cm 单元，80³，烟雾最大半径 4 m。服务器侧仍使用二值 sparse brick 和 snapshot/delta；客户端先生成向内 2 个单元（20 cm）的羽化 R8 密度，再选择 GPU 布局：

- Dense R8：一张 80³ R8 纹理，硬件三线性采样。
- Brick R8 buffer：10³ root table，8³ brick；混合块存 R8 字节，手动重建 8 个插值样本。
- Brick R8 atlas：相同 root table，混合块放入 R8 3D atlas，一次硬件三线性采样。

Root entry 区分全空、全满和混合块。按 `floor(uv*N-.5)` 选块，所以混合块仅需正方向一圈 apron：8³ 内部加边界共 9³。空/满判定包含 apron，避免丢失跨块过滤贡献。这两条 GPU 稀疏路径存的是羽化后的 **8-bit 密度**，不是直接采样网络二值 bitset。

三条路径供 extinction splat、MSM 和 accumulation 共用。逻辑分辨率始终为 80³，不因 atlas 大小改变 raymarch 的密度采样数。着色器通过 uniform header 选择布局，没有为每种布局单独编译专用 shader。

## 数据量

当前开门示例有 564 个混合 GPU brick，以下仅统计资源有效载荷，不包含 D3D12 heap 对齐、上传临时资源或公共外观纹理：

| GPU 布局 | Buffer | 纹理 | 合计 |
|---|---:|---:|---:|
| Dense R8，含可选跳空 root | 4,032 B | 512,000 B | 503.9 KiB |
| Brick R8 buffer | 416,880 B | 1 B 占位 | 407.1 KiB |
| Brick R8 atlas | 4,032 B | 531,441 B | 522.9 KiB |

Atlas 的 apron 和立方体打包空槽抵消了此场景的稀疏收益。网络快照另计：本例为 31,127 B；网络压缩与 GPU 采样布局可以独立选择。

## GPU 实测

内部 2560×1440，半分辨率烟雾，TAA 开启，extinction=6，FP16 accumulation/composition。DX12 StablePowerState 成功开启；每组预热 30 帧，计时 60 帧取中位数，最后重测 dense 基线。以下为 **volume accumulation 单 pass**，不包含 upsample、MSM 和其他管线开销。

| 场景 / 固定步长 | Dense | Brick buffer | Brick atlas |
|---|---:|---:|---:|
| Sponza 近景 / 10 cm | 3.944 ms | 4.862 ms | 4.761 ms |
| Sponza 体积内部 / 10 cm | 3.346 ms | 3.654 ms | 3.637 ms |
| 简单场景远景 / 10 cm | 0.971 ms | 1.301 ms | 1.254 ms |
| Sponza 近景 / 50 cm | 1.679 ms | 2.309 ms | 2.213 ms |

近景 10 cm 的 dense 重测为 3.943 ms，50 cm 重测为 1.680 ms。原 Sponza camera 4 被墙遮挡，不作为远景性能依据；另用无 Sponza 的简单场景 camera 4 测试可见烟雾。

近景 10 cm 全帧分别为 10.442 / 11.466 / 11.357 ms。额外查询也影响其他共享采样的 pass：MSM 为 0.201 / 0.258 / 0.239 ms，extinction splat 为 0.505 / 0.542 / 0.547 ms。

独立打开 Gameplay brick skipping 后，近景 10 cm 三条路径分别为 4.485 / 5.373 / 5.280 ms；50 cm 为 1.808 / 2.425 / 2.331 ms，均更慢。当前跳空沿用已有 DDA：在证明为空的区间跳过密度/光照，但外层采样格点和 T 查询仍然执行，并非直接推进外层循环到下一块。它的检查成本在此场景超过收益；这不代表缓存区间、直接跳跃的所有实现都会更慢。

本次是 GPU timestamp 对照实验，未采集硬件计数器，不能据此把差值精确归因于 ALU 或显存带宽。可确定的是：这份 80³ 数据下，当前层级查表与过滤路径没有胜过直接硬件采样。

## 质量与验证

- CPU 单元测试逐个检查所有 brick 的 9³ 内部及 apron，重建值与原始 R8 一致。
- GPU 捕获检查 numerator、denominator、total-tau、back-numerator 均有限且非负。
- 各存储路径对照相同相机、帧数、密度；测试截图最大差异为 1 个 8-bit 色阶。体积内部截图完全一致。此结论针对记录的测试视角，不是所有动态场景的等价性证明。
- 开关跳空在各自布局下的记录截图一致。

复现脚本：`avboit/tests/profile_grenade_storage.py`；默认跑 Sponza，`--far-open` 跑无遮挡远景，`--coarse` 跑 50 cm 步长。原始命令、日志、CSV、截图和 summary.json 分别保存在 `build/grenade-storage*`，不加入仓库。

目前仍是 synthetic gameplay coverage 与本地 authority/replica 回环，详见 [Gameplay smoke](GAMEPLAY_SMOKE.zh-CN.md)。默认用展开 R8 保住 GPU 性能，网络继续用稀疏结构节省复制数据；无需让两端使用相同存储布局。
