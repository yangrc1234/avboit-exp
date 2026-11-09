# SPDX-License-Identifier: MIT
"""Bake a sustained smoke source with Blender's Mantaflow, without UI dependencies.

blender --background --factory-startup --python tools/bake_smoke_vdb.py --
    --output build/smoke-grenade/final128 --resolution 64 --frames 144 --noise-scale 2

This is an offline asset probe, not a runtime simulation. The cache contains
OpenVDB frames, with no dependency on a Python OpenVDB module.
"""

import argparse
import math
from pathlib import Path
import sys

import bpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--resolution", type=int, default=64)
    parser.add_argument("--frames", type=int, default=144)
    parser.add_argument("--preset", choices=("grenade", "probe"), default="grenade")
    parser.add_argument("--noise-strength", type=float, default=0.25)
    parser.add_argument(
        "--noise-scale",
        type=int,
        default=2,
        help="High-resolution density multiplier; 1 disables noise",
    )
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1 :])
    if args.resolution < 16 or args.frames < 2 or not 0 <= args.noise_strength <= 10:
        parser.error("Use resolution >= 16 and frames >= 2")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any((output / "cache").glob("**/*.vdb")):
        raise RuntimeError("Output already contains a VDB bake; choose a new output directory")

    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = args.frames
    scene.render.fps = 24

    grenade = args.preset == "grenade"
    bpy.ops.mesh.primitive_uv_sphere_add(
        radius=0.3 if grenade else 0.65, location=(0, 0, 0.45 if grenade else 1.0)
    )
    source = bpy.context.object
    source.name = "Smoke source"
    flow = source.modifiers.new("Smoke flow", "FLUID")
    flow.fluid_type = "FLOW"
    bpy.context.view_layer.update()
    settings = flow.flow_settings
    settings.flow_type = "SMOKE"
    settings.flow_behavior = "INFLOW"
    settings.density = 1.0
    settings.temperature = 0.25 if grenade else 0.8
    settings.use_initial_velocity = True
    settings.velocity_coord = (0.25, 0.0, 1.0)
    if grenade:
        # A continuous, gently varying jet drives the flow. Fine structures must
        # be advected by Mantaflow, not animated as a runtime density noise.
        for frame in range(1, args.frames + 1, 4):
            t = (frame - 1) / 24.0
            settings.velocity_coord = (
                0.55 * math.sin(t * 2.1),
                0.35 * math.cos(t * 1.7),
                1.1 + 0.2 * math.sin(t * 3.4),
            )
            settings.keyframe_insert("velocity_coord", frame=frame)
    settings.use_inflow = True
    settings.keyframe_insert("use_inflow", frame=1)
    emit_end = max(1, args.frames * 2 // 3 if grenade else args.frames // 3)
    settings.keyframe_insert("use_inflow", frame=emit_end)
    settings.use_inflow = False
    settings.keyframe_insert("use_inflow", frame=emit_end + 1)

    bpy.ops.mesh.primitive_cube_add(size=2, location=(0, 0, 3))
    domain = bpy.context.object
    domain.name = "Smoke domain"
    domain.scale = (3, 3, 3)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    modifier = domain.modifiers.new("Smoke simulation", "FLUID")
    modifier.fluid_type = "DOMAIN"
    bpy.context.view_layer.update()
    settings = modifier.domain_settings
    settings.domain_type = "GAS"
    settings.resolution_max = args.resolution
    settings.use_adaptive_domain = False
    settings.use_noise = grenade and args.noise_scale > 1
    if settings.use_noise:
        settings.noise_scale = args.noise_scale
        settings.noise_strength = args.noise_strength
        settings.noise_pos_scale = 1.5
        settings.noise_time_anim = 0.1
        settings.cache_noise_format = "OPENVDB"
    settings.vorticity = 0.035 if grenade else 0.8
    if grenade:
        # Avoid the test puff's strong buoyancy and rapid loss through the domain.
        settings.alpha = 0.15
        settings.beta = 0.5
        settings.use_dissolve_smoke = False
        settings.cfl_condition = 2.0
        settings.timesteps_min = 2
        settings.timesteps_max = 8
        settings.use_collision_border_bottom = True
    # Bake flow and noise together. Blender 2.93 can otherwise emit empty noise
    # grids when the non-resumable base cache lacks the inflow/UV state.
    settings.cache_type = "ALL" if settings.use_noise else "MODULAR"
    settings.cache_resumable = settings.use_noise
    settings.cache_data_format = "OPENVDB"
    settings.openvdb_cache_compress_type = "ZIP"
    settings.cache_directory = str(output / "cache")
    settings.cache_frame_start = 1
    settings.cache_frame_end = args.frames
    scene.frame_set(1)
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "smoke.blend"))
    result = bpy.ops.fluid.bake_all() if settings.use_noise else bpy.ops.fluid.bake_data()
    if "FINISHED" not in result:
        raise RuntimeError(f"Bake failed: {result}")
    # Blender 2.93 can crash when reading density_grid directly after baking.
    # Validate exported grids in a separate Blender process instead.
    stage = "noise" if settings.use_noise else "data"
    files = sorted((output / "cache" / stage).glob("*.vdb"))
    if len(files) != args.frames:
        raise RuntimeError(f"Expected {args.frames} VDB frames, got {len(files)}")
    print(
        f"BAKE_OK frames={len(files)} bytes={sum(f.stat().st_size for f in files)}",
        flush=True,
    )


if __name__ == "__main__":
    main()
