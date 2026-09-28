# SPDX-License-Identifier: MIT
"""Validate native timer readback / CSV structure, not a performance threshold."""
import csv
import math
import subprocess
import tempfile
from pathlib import Path

viewer = Path(__file__).resolve().parents[2] / "bin/avboit_viewer.exe"
with tempfile.TemporaryDirectory(prefix="avboit-timers-") as scratch:
    for detail in (True, False):
        command = [str(viewer), "--benchmark", "60", "--csv", "timing.csv"]
        if not detail:
            command.append("--profile-frame-only")
        result = subprocess.run(command, cwd=scratch, capture_output=True)
        if result.returncode:
            raise RuntimeError(
                result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace")
            )
        text = Path(scratch, "timing.csv").read_text()
        assert "# skipped_frames=0" in text
        assert "stable_power_state=1" in text
        assert b"StablePowerState enabled for benchmark" in result.stdout
        assert b"StablePowerState restored after benchmark" in result.stdout
        rows = list(csv.DictReader(line for line in text.splitlines() if not line.startswith("#")))
        assert len(rows) == 60
        assert len(rows[0]) == (20 if detail else 2)
        frames = [int(row["frame_id"]) for row in rows]
        assert frames == sorted(set(frames))
        for row in rows:
            values = {k: float(v) for k, v in row.items() if k.endswith("_ms")}
            assert all(math.isfinite(v) and v >= 0 for v in values.values())
            assert values["frame_ms"] > 0
            # Timestamp granularity can round adjacent intervals differently.
            assert sum(v for k, v in values.items() if k != "frame_ms") <= values["frame_ms"] + 0.02
        print(f"{'Detailed' if detail else 'Frame-only'}: 60 valid GPU timing rows", flush=True)
