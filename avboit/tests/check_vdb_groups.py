# SPDX-License-Identifier: MIT
"""Check group ownership, including an interface that intersects the volume."""
import numpy as np

from capture_utils import image, texture
from check_vdb_compute import capture


if __name__ == "__main__":
    for name, flags in (("smoke", []), ("glass", ["--stress", "25"]),
                        ("anchors", ["--volume-eight-t", "--volume-step-scale", "3"])):
        common = ["--no-volume-early-out", *flags]
        actor = capture("groups-check-" + name + "-actor", [*common, "--vdb-proxy-stencil"])
        groups = capture("groups-check-" + name + "-groups", [*common, "--vdb-group-march"])
        for target in ("total-tau", "numerator", "denominator", "back-numerator"):
            a, b = (texture(folder / f"native-{target}.raw", "<f2", 4)[..., :3].astype(np.float32)
                    for folder in (actor, groups))
            if target == "total-tau" or (name == "glass" and target == "back-numerator"):
                assert a.max() > .1 and b.max() > .1, (name, target)
            error = np.abs(a - b)
            assert np.isfinite(error).all() and error.mean() < .001, (name, target, error.mean())
        color_error = np.abs(image(actor) - image(groups))
        assert color_error.max() <= 2 and color_error.mean() < .02, (name, color_error.max())
        print(name, "passed")
