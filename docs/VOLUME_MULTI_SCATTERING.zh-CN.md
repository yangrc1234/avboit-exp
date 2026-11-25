# 体积多 octave 散射近似

Scene → Volume lighting 可调整额外 octave 数（0–3）、贡献和 extinction scale。默认 2 个额外 octave，贡献 0.6、厚度倍率 0.4；0 为单次散射。CLI：`--volume-ms-octaves 0`。参数即时生效并重置 TAA 历史。

参考 [Epic 的体积云多重散射说明](https://dev.epicgames.com/documentation/unreal-engine/volumetric-cloud-component-in-unreal-engine)：以多个衰减尺度的光透射近似多次散射。本项目复用已有 MSM 光学厚度，不增加沿光线的 raymarch。

```text
tau = MSM 查询得到的灯光到采样点的光学厚度
response = exp(-tau)
for i = 1 .. additionalOctaves:
    response += contribution^i * exp(-tau * extinctionScale^i)
```

太阳和聚光灯分别计算此响应，再乘光强、几何阴影、各向同性相函数和原有 source tint。当前所有 octave 均为各向同性，无需再逐 octave 衰减相函数的 g。直接读取 tau，避免先生成接近零的 T，再由 log 恢复厚度的精度问题。

这是一种外观近似，不是路径追踪或严格能量守恒的多重散射。source tint 沿用原来的美术参数，没有变成每次碰撞都乘一次的物理 albedo。响应可能大于 1；它仅用于散射光照，不能代替透射率。环境光常数不变。墙体等不透明遮挡依然作用于所有 octave，因此无法模拟光绕过墙角进入烟雾；单纯提高 octave 不能解决所有暗部。

MSM 采样和矩重建只做一次，然后复用 tau 计算额外指数项。原有 cascade 接缝在每个响应计算完成后混合。没有新 RT、纹理采样或 pass；灯光参数 buffer 增加一个 float4。opaque 表面的烟雾投影保留原始 `exp(-tau)`，相机 extinction/T-LUT/Zero-T 也不修改。

## 2026-11-17 验证

RTX 3070，2560×1440，半分辨率体积，50 cm 步长，FP16 accumulation/composition，extinction=6，sun=6，spot=0，动画相位固定 4 秒，TAA。StablePowerState 开启，每组预热后计时 60 帧取中位数：

| 设置 | Volume accumulation |
|---|---:|
| 0 octave | 2.011 ms |
| 2 个额外 octave | 2.018 ms |
| 0 octave 重测 | 2.006 ms |

本组增量接近计时波动，不能推广为任意显卡或多灯场景都免费。全屏 numerator 均值由 0.0542 变为 0.0992，仅代表此次画面的累积散射贡献，不是最终图像亮度翻倍。

HDR 输出有限且非负。0/2 对照的 denominator、total-tau、完整 T-LUT、三个 MSM 的 moments/tau 数据一致。命令、CSV、日志和截图在 `build/multi-scatter-check`，不发布到仓库。
