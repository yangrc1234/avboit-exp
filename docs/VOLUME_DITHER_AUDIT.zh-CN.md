# 半分辨率 dithering 审计（历史对照）

当前已统一为 coherent 四邻居重建；下述旧核、选择菜单和选择参数仅为历史实验记录，
已从运行时代码删除，旧对照命令不再适用。现有 `--volume-upsample-test` 比较 coherent
与其无抖动 fallback，保留静止／移动镜头捕获。

第一轮审计只增加测试捕获；第二轮新增独立 shader 对照入口，默认仍保留旧核，TAA 参数不变。测试位置：`ViewerSelfTests.inl::TestTemporalAA`。`--volume-upsample-test --composition-half` 额外输出匹配的 TAA 前/后中央半宽半高 HDR crop 和逐帧 jitter/noise phase CSV；每个原始像素都保留，避免只采到同一种 2×2 相位。RGBA16F 前置输出才开启 HDR 捕获，原 packed 测试仍只输出原有 PPM。

## 已确认的链路

- Camera[9].w 每帧改变；raymarch 和 upsample 都使用该序列索引。
- 半分辨率每帧完整绘制，射线使用 `2*lowPixel + 1.5`，对应 2×2 的右下位置；WorldRay 再减去全分辨率尺度的 Halton jitter。射线确实随 jitter 变化，但没有轮换四种 full-res 子像素位置，也没有独立的缺失射线历史重建。
- Upsample 在 TAA 开启时使用四个随机 point taps，随后 bilateral 加权；无 TAA 时使用固定四邻域。
- TAA 在 distortion apply 后、显示/tonemap 前运行，history ping-pong 与 AdvanceFrame 正常。使用 Donut 的默认 newFrameWeight=0.1、3×3 variance history clamp；没有体积专用 history/motion/depth。
- Motion 来自主深度与相机，可能包含 Zero-T 深度。它不能表示所有透明层、折射和移动体积的真实 motion；静止相机验证不能替代运动验证。

## 点集的实际行为

按当前 shader 公式、32×32 蓝噪声表及256帧逐项枚举，2×2 中 `(even,even)` 像素的四个整数 texel 地址完全不变；另外三种相位的平均不同 texel 数分别约3.50、3.52、3.44。连续随机点落到纹理整数坐标后会重复，四象限分层并未保证四个不同 texel。

因此随机变化在四种像素相位之间不均匀。它是需要检查的结构性问题，但不等于最终图像一定比 fixed bilateral 更抖：fixed bilateral 自身在低分辨率像素中心接近单点读取，而随机版本在那里会混合更多邻居，空间核也变宽了。

## 实测

RTX3070，2560×1440，半分辨率体积，extinction=16，无 LUT skip/min 实验；同一程序内 fixed bilateral 与 dithered 各48帧。前32帧热身，记录之后8帧静止和8帧横移。CSV确认两组对应帧的 Halton jitter 和 raymarch noise phase 一致。下面只定量报告静止8帧，不能视作运动质量结论。

共同统计区域由 TAA 前两种方法出现变化的像素确定（最大RGB差异>0.002），位于中央crop；HDR时间标准差按RGB平均：

| 场景 | 固定bilateral TAA前/后 | dithered TAA前/后 |
|---|---:|---:|
| 测试场景24 | 0.03693 / 0.005684 | 0.02882 / 0.004568 |
| Sponza场景24 | 0.03019 / 0.004826 | 0.02171 / 0.003602 |

TAA确实在抑制波动；dithered最终波动比fixed低约20%/25%。它不是单纯添加无效噪声。不过TAA前波动就降低了，不能把所有收益归因于时间积累；空间平滑同时参与。

全分辨率、6.25cm步长、TAA稳定32帧作为数值参考（非ground truth），两种半分辨率方法的8帧均值与它比较：测试场景平均HDR误差0.06848→0.06760，Sponza 0.05146→0.05166，基本同量级。这个实验不支持“随机upsample明显恢复了更多全分辨率细节”的说法。

## 建议

保留当前随机重建，不凭噪声观感直接删除。下一步先保证离散tap不重复、让四种pixel phase具有相近的过滤/随机特性，并与匹配空间核的确定性过滤比较，才能分离随机化与变模糊的收益。若目标是逐帧补齐不同射线，应另外设计半分辨率射线的2×2相位轮换，并同步depth guide和重建坐标；仅改变upsample邻居不会达到这一目标。

重现：在独立输出目录运行 `avboit_viewer --volume-upsample-test --composition-half --stress 24 --volume-extinction 16`；Sponza加`--sponza`。保存到 `build/dither-audit/smoke16` 与 `sponza16`，再运行 `avboit/tests/analyze_volume_dithering.py`。参考运行去掉half/upscale-test，使用`--headless --taa --composition-half --volume-step .0625 --volume-step-scale 1 --volume-max-steps 512`。


## 第二轮：整体 jitter 与匹配空间核

通过 ImGui 的 `Quality → Volumes → Volume upsample` 实时切换；也保留命令行实验 `--volume-upsample-kernel N`，不增加 RT。切换时清空 TAA history 并保留 actor 材质编辑。下拉框额外提供 Fixed bilateral 基线；其余三项对应：

- `0`：原四象限随机 point taps，仍是默认。
- `1`：只生成一组二维蓝噪声，在 half-res 坐标中将采样中心偏移 ±0.5 texel，然后读取固定四邻域，以 bilinear × bilateral 权重归一化。屏幕内部四个 texel 不重复；屏幕边缘 clamp 仍可能重复。某些像素的地址仍不变，但权重会随时间变化。
- `2`：无随机性的匹配空间参考。对中心均匀 jitter 的 bilinear 核做解析时间平均，整数位置的一维权重为 `[1/8, 3/4, 1/8]`，半整数位置为 `[1/2, 1/2]`，最大 3×3 点读取。只匹配深度连续处的空间核；bilateral 归一化、非线性 OIT resolve、TAA clamp 后，不保证和随机版期望完全相等。

三个入口独立编译。没有 TAA 时都回到原 fixed bilateral；半分辨率射线、raymarch dithering、history 和 depth guide 不变。文件名中的 `dithered` 表示当前被测入口，kernel=2 实际没有 upsample 随机化。

### 画质对照

2560×1440、extinction=16；相同相机、raymarch noise 和 Halton phase，各组32帧热身后记录8帧静止HDR和8帧移动PPM。增加了烟雾穿插磨砂玻璃的场景25。三个入口的 fixed bilateral 控制组 HDR 完全相同，所有捕获有限值检查通过。共同 mask 现在包含所有新旧核的变化区域，所以绝对统计值不能直接与第一轮不同 mask 比较。

| TAA后HDR时间标准差 | 固定bilateral | 旧随机核 | 整体jitter | 匹配空间参考 |
|---|---:|---:|---:|---:|
| 烟雾24 | .005509 | .004433 | .004960 | .005143 |
| Sponza24 | .004570 | .003480 | .003869 | .004500 |
| 磨砂穿插25 | .005588 | .004423 | .004918 | .005284 |

整体 jitter 的四种像素相位更均衡：例如 Sponza 的相位标准差由旧核的 `.002782–.003945` 收敛到 `.003800–.003932`，但总体时间波动高约11%。不能把去除重复 tap 解释成必然减少最终噪声。上述数字仅覆盖短静止序列，移动捕获用于复查，尚无运动误差的定量结论。

确定性参考在 TAA 前更稳，但 TAA 后并未胜出，说明原随机核的效果不能完全解释为更宽的空间平滑；history clamp 和随机误差的时间相关性也需要考虑，当前实验不能分别确定各自贡献。

相同全分辨率6.25cm步长参考下，整体 jitter 的8帧均值误差只小幅改善（烟雾 .05196→.05171；Sponza .02793→.02762），不足以证明更好的细节重建。

### 耗时

RTX3070、2560×1440、packed默认输出、StablePowerState=1。各80帧，两轮反向顺序，表内为该 pass 中位数；画质捕获使用 composition-half，不将其耗时混入性能结果。

| Upsample GPU ms | 固定bilateral | 旧随机核 | 整体jitter | 匹配空间参考 |
|---|---:|---:|---:|---:|
| 烟雾24 | .068 | .288 | .082 | .115 |
| Sponza24 | .066 | .289 | .083 | .117 |

整体 jitter 将该 pass 降低约71%，省约0.206ms。Sponza 整帧中位数约4.237→4.035ms。另用改动前原始 shader 独立重复基线两次，upsample均为.289ms，最终PPM与保留的旧实现完全相同，排除了重构旧入口造成性能退化的情况。

主要代码差异是随机序列查找从四组二维值变成一组，4个颜色tap和相同RT输出仍保留。DXIL中静态蓝噪声表的索引次数从8变为2；这支持减少随机序列开销的解释，但没有用硬件计数器拆分收益。

结论：整体 jitter 是值得保留的性能候选，暂不自动替换默认。它解决了离散tap重复和明显的相位不均衡，但没有达到“噪声也更低”的目标。匹配参考仅用于实验。

### 重现

分别在 `build/dither-kernels/smoke16-{0,1,2}` 中运行：

```text
avboit_viewer --volume-upsample-test --composition-half --stress 24 --volume-extinction 16 --volume-upsample-kernel N
```

Sponza加`--sponza`，目录名`sponza16-N`；磨砂穿插改成`--stress 25`，目录名`frost16-N`。执行 `python avboit/tests/compare_volume_upsample.py` 生成 `report.json`，检查匹配相位、控制组一致性和有限值，并验证匹配核的数值积分。

性能测试用 `--headless --taa --volume-half-res --stress 24 --volume-extinction 16 --benchmark 80 --csv <path> --volume-upsample-kernel N`；固定bilateral使用`--volume-upsample-bilateral`。输出CSV必须注明`stable_power_state=1`。


## 噪声频率与 RDR2 的区别

核对原始讲稿第51–56页及备注：RDR2先在半分辨率目标中每个2×2 tile更新一条射线，对其余射线做历史重投影、深度约束的3×3 color-AABB clamp与缺失历史回退；之后才执行第56页的全分辨率蓝噪声四点upscale。深度连续时平均，深度不连续时选最近深度，并用透射率加权ray depth调节kernel宽度。原文承认欠采样和clamp仍可能闪烁，未承诺残差只有全分辨率pixel尺度。

我们每帧完整计算半分辨率像素，直接将N/A/tau/BackN上采样，然后做OIT resolve与最终TAA；没有中间体积history。半分辨率源图中的一个raymarch误差被多个输出像素共同采到，即使各输出像素的offset独立，误差也有空间相关性。写成`error(p)=sum_j weight(p,j)*error_low(j)`，相邻p/q共享的低分辨率样本使它们的误差相关。全分辨率dither可以打散采样格，不会凭空让源图误差彼此独立。

当前50cm默认步长，密度每段有4个分层tap，光照每段只算一次；同一ray的所有段共享sampleFraction。因此光照/密度欠采样是需要考虑的源误差，具体哪项占主导还需要冻结源图和缩短步长的对照，不能仅凭最终画面归因于upsample。

旧随机核还存在已测到的离散化问题：even/even相位的四个地址和空间权重不随noise变化，其余相位可能重复tap。整体jitter改善该不均衡，但仍会共享低分辨率源值。它不能等价于full-resolution raymarch，也不能称为RDR2完整复现。

更直接的下一步诊断是：固定raymarch采样相位，仅改变upsample相位；反过来固定upsample仅改变raymarch相位。若冻结源图后粗颗粒明显减少，先降低源图积分方差或增加轻量的体积history；不要仅通过扩大最终jitter半径掩盖它。本轮未改变噪声算法。

上述历史复现实验原先关闭T-LUT min-filter，且部分全分辨率参考没有TAA。新默认开启TAA、half-resolution和min-filter后，复现旧结果须显式使用`--no-lut-min-filter`，并对全分辨率参考加`--volume-full-res`；需要非时间基线时加`--no-taa`。现有check脚本已固定其原始对照条件。
