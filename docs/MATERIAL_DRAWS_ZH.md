# Actor / 材质提交边界

场景提交按 **actor + material section** 分开，每次 draw 只有一个材质。
不同 actor 即使复用同一材质，也不跨 actor 合批。一个 actor 有多个材质 section
时分多次 draw。CPU/GPU 可以共享 mesh buffer、材质参数 buffer、shader 编译结果，
这些共享不改变 draw 边界。

## 材质与 shader 集

`avboit/render/MaterialShaders.h` 给每个材质提供 vertex shader，以及 opaque、
extinction、interface、accumulation、distortion 对应的 pixel shader 和 cull mode。
各 pass 按这个 shader 集创建 PSO，根据 draw.material 选择。示例宿主当前复用公共
shader 实现；接入不同材质 shader 时替换对应 handle，保持各 pass 的资源和输出 ABI。
材质参数仍保存在现有表中，不要求把不同 shader 的材质合进一个 draw。

`sample/GeometrySubmission.h` 根据顶点携带的 actor/material identity 构建索引段，
保证一个三角形属于同一个 actor/material。extinction 与 accumulation 使用同一组
draw；interface 在此基础上仅选择需要特殊界面的材质。随机提交测试仍打乱 actor 的
首次出现顺序以及 section 内三角形顺序，不会打破材质 draw 边界。

不透明 fixture 也按 actor/material 索引段分别绘制；双层 VFX 分别提交，不再共用一个
包含不同 phase 材质的 12 顶点 draw。Sponza 原有材质 primitive 的提交保持独立。

## 单面玻璃球

测试球的材质在 extinction splat、interface prepass、accumulation 三处均使用
硬件 CullBack。球的解析折射函数仍计算入射/出射，因此仍能看到倒像。不会再把后半球
作为额外 OIT 层写入 BackN，也不会再多算后半球的消光。没有增加 RT，没有 PS 背面 discard。
其他双面玻璃片、烟雾等材质保留各自的剔除设置。

验证：`python avboit/tests/check_single_sided_sphere.py` 在 2560×1440 下检查正序与乱序
提交，要求 interface 保留前半球、totalTau 和 LUT 只包含一层、BackN 完全为零。

RenderDoc 的几何 marker 为 `Extinction/Interface/Accumulation/Opaque actor N / material M`；
VFX marker 也包含 actor/material。新增的是索引列表和 PSO/提交边界，OIT 的四张 MRT
及后续 B′、Gaussian、resolve 管线不变。draw 数增加，旧的整体 GPU/CPU 耗时不可直接沿用。
