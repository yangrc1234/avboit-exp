# avboit-exp

一个基于 **DX12 / C++17 / HLSL** 的 AVBOIT 实验，加入了磨砂玻璃、屏幕空间折射和独立 VFX 扭曲。
窗口、场景和 ImGui 使用 Donut；GPU 提交使用 NVRHI；透明算法位于独立的 `avboit/render` 模块。

这是根据 Michal Drobot 的 SIGGRAPH 2025 公开演讲独立实现的研究原型，
不是 Activision 源码，也不宣称是论文的官方参考实现。

![不同粗糙度的磨砂玻璃与 HDR 自发光物体](docs/images/frost.png)

程序化场景的 2560×1440 截图：竖条使用不同粗糙度，圆孔用于对照未模糊的背景；不依赖外部模型。

## 快速运行

需要 Windows x64、Visual Studio 2022 C++ 工具和 Windows SDK、CMake 3.18+、Python 3.9+、DX12 GPU。

```powershell
git clone --recursive https://github.com/yangrc1234/avboit-exp.git
cd avboit-exp
python build_native.py
bin/avboit_viewer.exe
```

默认只构建 viewer，`bin` 中只有一个应用 exe 和必需的 shader。ShaderMake 放在 `build/tools`。
默认渲染分辨率为 **2560×1440**，帧率上限 **120 FPS**，都可以在 ImGui 中调整。
程序化测试场景不依赖外部模型。

Sponza 按需下载，不随代码或运行包分发：

```powershell
python tools/fetch_sponza.py
bin/avboit_viewer.exe --sponza
```

下载脚本校验固定版本的每个文件；可附加 `--proxy http://HOST:PORT` 使用单次代理。
可以在 ImGui 的 Background / Test case 中切换场景；Scene fixtures 修改后立即生效。
WASD/QE 移动、右键转视角、Home 复位；B 切换 bilinear/bicubic，G 开关磨砂，V 开关 VFX，空格暂停动画。

## 当前方案

- occupancy + adaptive Z，packed atomic extinction，RGBA8 的 T-LUT；默认 XY 为 1/8，Z 为 128 slices。
- 全分辨率 `N / A / totalTau / BackN` 四 MRT，支持 RGB transmittance。
- 每像素选最近的特殊界面；支持同一界面的折射 + 磨砂。
- B′ 只缓存透明 tile 及一像素边界；其他源位置直接读取 opaque。
- 材质定义屏幕空间模糊宽度，不依赖背景深度；默认 3 级 Gaussian，更高档增加精细 mip。
- 主 Resolve 写临时 tile color，再拷回 SceneColor；VFX offset 与 HDR apply 独立。
- RenderDoc marker、shader 源码调试信息、occupancy overlay、每 pass GPU 计时。

详细公式和资源表：[当前管线](docs/PIPELINE.zh-CN.md)。
材质提交边界：[actor/material draws](docs/MATERIAL_DRAWS_ZH.md)。
局限包括单特殊界面、屏幕空间前景采样、packed 精度和高 overdraw 溢出、Zero-T 的背景残留，
以及未接入 TAA/MSAA，见 [已知限制](docs/KNOWN_LIMITATIONS.zh-CN.md)。

## 测试与发布包

```powershell
python build_native.py --tests
python tools/package_runtime.py --output dist/avboit-exp
python tools/check_runtime_package.py --package dist/avboit-exp
```

测试 exe/shader 位于 `build/tests/bin`，不进入运行包。`--cpu-tests` 用于没有 DX12 GPU 的 CI。
GPU 测试串行执行；画质验证默认 2K，小尺寸用例只验证资源尺寸处理。
测性能时使用 StablePowerState，避免把频率波动误当优化收益；正常交互不启用该模式。

代码格式、回归入口见 [贡献说明](CONTRIBUTING.md) 和 [验证记录](docs/VALIDATION.md)。
代码采用 MIT，保留 NVIDIA 上游版权。第三方依赖、Sponza 的许可单列于
[授权清单](THIRD_PARTY_NOTICES.md)，项目 MIT 不替代它们的许可。
