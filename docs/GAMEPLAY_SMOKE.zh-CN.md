# Gameplay 覆盖烟雾弹原型

运行 `tools/preview_gameplay_grenade.ps1`。在 Scene preset 直接选择 `Gameplay smoke grenade`。Scene → Smoke grenade 调整扩散半径（米）和流动速度，Actors 编辑体积 extinction/散射颜色。Quality → Volumes 只保留 GPU 存储和采样质量。界面保留 10 个演示入口，旧回归场景仅通过 CLI `--stress` 使用。

本版不依赖外部 VDB 动画：服务器侧模拟生成 0/1 覆盖，客户端重建后交给现有 extinction、T-LUT、raymarch、体积阴影、half-resolution upsample 和 TAA。随程序提供的密度纹理仅用于客户端外观调制，不定义覆盖范围。已添加细节流动动画，尚无流体模拟；扩散滑条表示一个 gameplay 阶段。

## 数据层级

两级 sparse brick：粗网格通过 brick index 定位，8×8×8 个细体素存为 512 bit。空 brick 不存，全满 brick 在传输时只存标记，混合块传 64 字节位图。并非递归 octree，第一版避免引入不必要的层级遍历。

默认 80³，cell size 100 mm，整体 8 m 的立方体，最大烟雾半径 4 m。较大范围可以修改尺寸或格距，不能把同样的体素数任意拉伸还声称精度不变。实际接入应由服务器的 grid origin、cell size 决定 actor bounds。当前示例绑定场景 24 的默认位置；actor 编辑器移动/旋转只改变本地显示，不会回传 authority。

客户端 GPU 有 Dense R8、Brick R8 buffer、Brick R8 atlas 三种可切换路径，并可单独开启保守 brick 跳空。分层路径存储羽化后的 R8，不是网络层的二值 bitset；不能把网络压缩比当成 GPU 显存压缩比。CPU 以距离变换向内羽化两个体素（20 cm），改善边界；过滤可能在边界附近产生亚体素混合，gameplay 判定始终读取原始二值数据。

## 服务器同步接口

`sample/SmokeCoverage.h` 不依赖图形 API，可独立放到服务器：

- `Encode()` 输出完整快照。
- `Encode(&acknowledgedBaseline)` 输出相对客户端已确认版本的增量；删除块也编码。
- 客户端 `Apply(packet)` 完整校验后一次提交，不能半更新。
- Header 包含协议版本、smoke ID、revision、base revision、网格尺寸、毫米原点/格距和视觉 seed。
- wire format 显式 little-endian，不发送 C++ 内存布局；FNV checksum 检查损坏，不提供安全认证。
- 重复、旧版本、缺失基线、损坏或截断的包被拒绝；缺失增量后应请求 snapshot。
- 新加入客户端直接接收 snapshot。每个 smoke instance 独立维护状态；重生/换 ID 创建新的接收器。

viewer 内有独立 authority 与 replica，通过真实编码后的字节包更新 replica；渲染只使用 replica。**这是本地序列化回环，不是已接入 UE replication 或真实网络的服务器。** 可靠传输、MTU 分片/重组、ACK、限频、兴趣范围和身份认证应由工程网络层提供。对未确认基线不能直接继续发链式 delta。

旧 128³ 单元测试案例：关门 snapshot 32,949 B，开门 delta 15,584 B，缩小覆盖的删除更新 12,847 B。旧 80³、带虚拟墙且开门的 4 m 半径示例快照为 31,127 B；全满的 8³ 单块快照为 57 B。包尺寸随形状和边界数量变化，不按每帧发送。游戏只同步覆盖变化；光照、T-LUT、外观纹理不复制。

## 示例与验证范围

烟雾弹演示使用六邻域 flood-fill 和球形范围限制，默认没有障碍。早期不可见的虚拟墙与门洞会把烟雾中部切出直边，现仅保留在数据复制单元测试中，不参与演示覆盖。**尚未读取 Sponza 实际碰撞或门 actor**；正式工程应由 gameplay 提供场景遮挡后的覆盖数据。

`avboit_smoke_coverage_test` 验证全满/混合 brick、snapshot/delta 一致性、删除块、重复/乱序更新、丢包后快照恢复、损坏/截断包的原子拒绝，以及门关闭的阻断。2K open/closed 截图位于 `build/gameplay-grenade`。

GPU 实现、性能及过滤误差见 [GPU storage 对比](GRENADE_STORAGE.zh-CN.md)。

## 径向基础密度与减法侵蚀

客户端密度（2026-11-17）改为：

```text
r = distance(position, center) / gameplayRadius
base = saturate(1 - r*r)
erosion = 0.45 * (1 - saturate(detailTexture))
density = coverage * max(base - erosion, 0) * 2.5
```

半径随扩散滑条改变，存于 GPU header；0/1 gameplay 数据及网络包不变。细节使用离线生成的周期 Perlin–Worley R8 噪声、2.3 次平铺，值高处侵蚀较弱。64³ 内部加双侧 wrap border 存为 66³，现有 clamp sampler 也能无缝过滤。没有额外纹理采样。覆盖始终乘在最后，侵蚀只改变视觉形状，不扩大 gameplay 区域。extinction、MSM 和 accumulation 共用此函数。

细节噪声现在使用缓慢平移加周期 domain warp 产生流动，属于视觉动画而非流体模拟。Scene → Smoke grenade 提供 Flow animation、Flow speed 和 Reset flow；不驱动场景灯光，也不改变 gameplay 覆盖。噪声由 tools/generate_smoke_noise.py 确定性生成；低频 Perlin 与多频 Worley 离线合成，运行时一次采样。当前更透明的区间会改变低 T 跳步和光照工作量，因此不能沿用旧密度模型的性能数据。

动画时间通过 Camera[2].w 传给 splat/accumulation，通过 VolumeLights[38].y 传给 MSM；同一帧相位一致。暂停保持当前相位，速度仅影响后续时间推进。没有新增 RT 或纹理采样，新增的是坐标变换和三个正弦运算。CLI `--time` 可指定烟雾弹捕获的初始动画相位。

Scene → Volume lighting 现支持 [多 octave 散射近似](VOLUME_MULTI_SCATTERING.zh-CN.md)，默认额外 2 层；设为 0 可比较单次散射。

## 弹道 capsule 穿烟原型

Scene → Smoke grenade → Bullet smoke interaction 支持单发、连续自动射击和 15 发同时测试。F 沿当前相机前方射击，左键仍用于 actor 编辑；半径默认 0.45 m，恢复时间 2 s，自动射速 5 发/s。自动射击瞄准烟雾中心周围，手动射击沿相机前方。半径表示可见扰动范围，不是子弹口径。没有加入碰撞查询；工程中应传入已由 gameplay 截断的弹道端点。

`SmokeTrails::Add(start, end, radius, lifetime)` 接收世界空间米制 capsule，最多 15 个，超出替换最早一条。前 40% 生命周期保持挖空，后 60% 用 smoothstep 恢复。零长度线段按球处理。互相重叠时取最大挖空强度，避免叠乘导致过强效果。

GPU capsule 参数为一个 528 B typed buffer：1 个 float4 header、15 对 start.xyz/radius 与 end.xyz/strength，以及 LUT 原点和 world-to-cell 比例。当前默认通过下述候选 LUT 筛选 capsule。`SampleSmokeDensity` 里在 gameplay 分支统一乘上 capsule 可见性，extinction splat、accumulation 和 MSM 都使用世界空间同一批数据；不改写原始覆盖或复制包。现有 brick 跳空仍保守，因为 capsule 只能减少密度。

这是解析距离场挖空，不是流体推开模拟。0.5 m raymarch、低分辨率 T-LUT、half-resolution 和 TAA 会限制细通道的精度，特别是斜穿烟雾和很小半径；不要把它视为精确 gameplay 视线判定。TAA 不因每一发而重置，短暂通道可能有历史延迟。

CLI `--gameplay-grenade --smoke-shots 0|1|15` 提供固定的效果与性能捕获。以下是引入 LUT 前的原型数据（2026-11-17），RTX 3070、2560×1440、half-resolution、TAA、50 cm 步长、默认远处烟雾、StablePowerState 开启，60 帧中位数：

| 同时 capsule 数 | Volume accumulation | Extinction splat | Shadow（含 MSM） |
|---|---:|---:|---:|
| 0 | 0.595 ms | 0.121 ms | 0.176 ms |
| 1 | 0.743 ms | 0.135 ms | 0.201 ms |
| 15 | 2.987 ms | 0.354 ms | 0.461 ms |

捕获位于 `build/smoke-trails`。表中比较的是实际挖洞后的工作负载，包含密度/透射率变化，不能当成纯 capsule ALU 的隔离测量。旧版每个密度采样遍历有效 capsule，15 条时成本偏高，现已默认使用候选 LUT。

## Capsule 候选 LUT

新增 `sample/SmokeTrailLut.*` 与 `shaders/SmokeTrailLut.hlsl`。16³ 的 R32_UINT Texture3D 存每格最多 15 位候选 bitmask；逻辑数据 16 KiB，实际分配还受 D3D12 资源对齐影响。它不存密度，也不把胶囊边界量化到格子上。

每格一个 compute thread，使用格子外接球与 capsule 的保守相交测试。可能多标，不允许漏标。全格写入，不用 clear、原子或列表。默认 8 m 轴对齐范围对应 50 cm 格子；范围取当前 textured volume 的世界 AABB 并集，包含 actor 旋转后的 extent。多个远隔 volume 会令格子变粗、候选增多，但不会因此丢精度。

采样端整数 `Load` 候选 mask，`firstbitlow` 遍历置位项，仍以完整 world-space capsule 解析求距离、软边和恢复强度。LUT 外部位置回退全量解析，不 clamp 到边缘格子。零 capsule 直接返回，不读取未初始化 LUT；只有一条 capsule 时直接解析计算，跳过索引读取和重建。extinction、accumulation 和 MSM 共享相同索引和参数。

只在端点、半径、有效数量、slot 排列或 volume bounds 变化时重建。单纯强度随年龄衰减不重建；到期移除导致 slot 压缩时会重建。RenderDoc 与 GPU timer 中独立标记为 `Smoke capsule candidate LUT` / `smoke_trail_index`，计时包含参数上传与必要的 dispatch/资源转换。

离线比较保留 CLI `--smoke-trail-reference`（原始逐条计数循环）、`--smoke-trail-rebuild`（每帧强制重建，用于测更新成本）、`--smoke-shot-radius`（米）。不在 ImGui 增加对比开关。运行 `avboit/tests/profile_smoke_trail_lut.py` 可复现 0/1/15 条、1 cm 细弹道及近距离对比，检查截图、accumulation 和光空间 tau 一致性。

最终计数循环 reference 对比（RTX 3070、2560×1440、half-resolution、TAA、StablePowerState、60 帧中位数）：

| 场景 | 全量解析 accumulation | LUT accumulation |
|---|---:|---:|
| 15 条，默认距离 | 3.351 ms | 1.411 ms |
| 15 条，半径 1 cm | 2.429 ms | 0.857 ms |
| 15 条，近距离 | 6.657 ms | 3.063 ms |

每帧强制重建的 index 阶段约 0.010 ms（含上传）；正常复用时约 0.001 ms。所有对比的最终截图最大差值为 0，numerator/denominator/totalTau 和三层 light tau 二进制一致。结果在 `build/smoke-trail-lut-final/summary.json`。索引保持已测试画质，但不意味着现有 raymarch 本身能解析所有细小通道；近距离 accumulation 仍未达到 1.5 ms。
