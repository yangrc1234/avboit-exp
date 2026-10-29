# 局部体积与动态阴影

启动 `bin/avboit_viewer.exe --sponza --stress 24` 看烟雾；`--stress 25` 加磨砂玻璃。
ImGui 的 **Dynamic lighting / volume shadows** 可以实时调整方向光、聚光灯和 MSM。
聚光灯默认随时间移动；总动画暂停时也会停止。

相机方向仍走 AVBOIT：体积的完整 bounds 参与 occupancy，低分辨率绘制注入消光，
全分辨率 raymarch 查询 T LUT，把散射贡献累加到原来的四张 RT。没有单独的体积合成。

每盏灯有两类阴影：

- **普通 shadow map**：记录 Sponza 不透明／Masked 几何深度，Reversed-Z。
- **体积 MSM**：记录局部体积的总光学厚度和四阶 power moments，重建任意深度的透射率。
  方向光用两级 cascade，聚光灯用透视投影；每级 256×256。

体积采样点的入射光乘上普通阴影可见度和 MSM 透射率，所以既有建筑遮挡，也有烟雾自阴影。
Sponza 的表面直接光照也会查询 MSM，接收烟雾投影。
根据世界空间体积 bounds 判断灯／cascade 是否需要 MSM，不剔除镜头外的投影体积。
没有相关体积时跳过生成和查询；没有体积的场景不分配完整 MSM 数组。

MSM 使用 **RGBA16_UNORM＋R16F**，三层共约 **1.875 MiB**。
先以 FP32 积分，再归一化并使用 MBOIT 作者的四阶 moments 量化变换写出；
读取时两张纹理各做一次硬件 bilinear，再解码。没有手动四 tap 重建。
归一化 moments 和总光学厚度独立滤波，在光学厚度突变处是近似。
静态烟雾、移动聚光灯和磨砂交互三组 2K 截图相对旧 FP32 基线最大差异为 1/255。
当前每层仍为 256×256；没有同时修改分辨率。
当前是标量体积阴影、各向同性单次散射和固定环境项；暂不支持点光源 cubemap、
透明玻璃向灯光投影，也不把测试用不透明 fixture 绘制到 Sponza 阴影图中。
更完整的算法、资源说明和验证方法见 [英文说明](LOCAL_VOLUMES.md)。

初步性能记录：RTX 3070，2560×1440，case 24，默认 packed RT，StablePowerState，
120 帧中位数，详细计时开启。整帧约 7.27 ms，accumulation 约 3.58 ms，阴影阶段
（普通聚光灯阴影＋体积 MSM）约 0.22 ms。跳过空密度光照查询、避免非过渡区重复
查询两个 cascade 前，accumulation 约 8.30 ms；优化前后测试截图逐像素一致。
这是单机原型数据，不代表低端显卡预算已达标。


## 半分辨率 accumulation

ImGui 的 Resolve pipeline 中可切换 `Half-resolution local volumes`，或使用
`--volume-half-res`。当前仅局部体积材质提供低分辨率 shader；interface 与其他透明材质
始终全分辨率。渲染器通过 `MaterialShaders::halfResolutionPixel` 显式选择可降分辨率材质。

顺序为半宽半高体积 accumulation → bilateral upsample 覆盖全分辨率四张 accumulation RT
→ 全分辨率透明对象 additive accumulation → 原有统一 resolve。Extinction/LUT 不改变。
低分辨率没有绑定全分辨率 DSV，shader 在选定的全分辨率 guide 像素读取深度并裁剪射线。
2×2 上采样对 N、A、totalTau、BackN 使用同一组空间/opaque depth/interface depth 权重；
没有可靠权重时使用深度最接近的候选。没有全分辨率补算，细遮挡与界面边缘允许近似。
禁用后释放四张半分辨率 RT。2K packed 格式额外占用约 14.06 MiB（不含驱动对齐）。

`Stochastic volume upsample (requires TAA)` / `--volume-stochastic-upsample`
是实验选项，给 full-resolution 像素使用独立的蓝噪声偏移，再执行相同的 bilateral 重建。
32×32 rank tile 由 `tools/generate_volume_blue_noise.py` 确定性生成，无第三方素材。
本示例没有为此新增 TAA 或体积历史缓冲；默认关闭此选项。单帧与有限性检查不代表
已验证 UE4 TAA / DLSS 的时域质量，需在目标工程中测试移动、显露和动态灯光。
