# Nsight 烟雾抓取（2026-11-06）

使用本机 Nsight Systems 2025.1.3，RTX 3070 / 驱动 617.14。没有安装 Nsight Graphics。
配置为 2560×1440、相机 10 近距离、JangaFX 128³ 4-bit 单帧、半分辨率烟雾、
10 cm 步长、TAA、coherent upsample；动画固定在 60.5（实际采样第 60 帧）。
应用 benchmark 开启 StablePowerState，30 帧预热，60 帧计时。

## 抓取结果

`nsys profile --trace=dx12,dx12-annotations --sample=none --cpuctxsw=none
--dx12-gpu-workload=individual --export=sqlite` 可以无管理员权限成功抓取。
`dx12-annotations` 必须显式开启，本机仅指定 `dx12` 时没有 pass marker。
现有 RAII GPU markers 无需改动。

| 范围 | Nsight GPU marker 中位数 |
|---|---:|
| AVBOIT Frame | 10.950 ms |
| Half-resolution volume accumulation | 5.063 ms |
| 其中烟雾 actor 0 / material 0 | 4.995 ms |
| Opaque | 2.268 ms |
| Extinction clear | 0.766 ms |
| Transmittance LUT integration | 0.508 ms |
| Volume depth-aware upsample | 0.375 ms |
| Extinction atomic splat | 0.177 ms |

Marker 统计包含捕获内的 143 帧（包括预热），父子范围重叠，不能直接相加。
同一次抓取中应用自身 60 帧 timestamp 中位数为 accumulation 5.021 ms、
upsample 0.372 ms，与 marker 量级一致。固定时间测试不覆盖连续动画上传。
启动时图形 PSO 创建累计约 6.6 秒是 CPU 启动成本，不是每帧 raymarch 成本。

主要 GPU 耗时确定在烟雾 actor 的 accumulation；不是半分辨率重建。
但这些计时不能进一步证明它是 ALU、缓存延迟、带宽还是分歧受限。

## 硬件计数器限制

Nsight 查询 GPU metrics 返回 `ERR_NVGPUCTRPERM / Insufficient privilege`。
准备了仅本次运行提升权限的 capture 脚本；Windows UAC 启动最终返回
“操作已被用户取消”。没有修改全局驱动权限，也没有取得硬件计数器。
因此本次没有 SM/TEX/DRAM 利用率、occupancy 或 shader stall 的实测结论。

下一次可在允许管理员运行后抓取 `ga10x-gfxt`，或用 Nsight Graphics
Shader Profiler 定位具体指令/源码热点。NVIDIA 官方权限说明：
https://developer.nvidia.com/ERR_NVGPUCTRPERM

## 结合源码的待验证方向

- `SampleSmokeFrame` 依赖 brick indirection 寻址，每次密度采样手工解码八个
  voxel 并插值；4-bit 仍有解码/寻址开销，资产缩小不代表采样指令减少。
- 空 brick 跳过密度和光照，但仍按原步进格点迭代、查询 T-LUT；不能把它当作
  一次直接跨越整个空区的 ray 跳跃。
- 非空区存在 T-LUT、密度、阴影等依赖读取。应以计数器或受控单项替换实验区分
  依赖访存延迟与计算成本，而不是凭总耗时认定 DRAM 带宽不足。

原始抓取（本地忽略目录）：
`build/nsight-smoke/annotations/near-annotations.nsys-rep`、同名 SQLite；
同目录 `command.json`、`marker-summary.json`、`native-benchmark.csv`。
准备好的管理员抓取脚本为 `build/nsight-smoke/capture-admin.ps1`。
