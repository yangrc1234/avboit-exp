# SPDX-License-Identifier: MIT
"""Format first-party sources only. Never traverse Donut or generated files."""

import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    formatter = shutil.which("clang-format")
    if not formatter:
        sibling = Path(sys.executable).parent / "clang-format.exe"
        formatter = str(sibling) if sibling.is_file() else None
    if not formatter:
        raise SystemExit("Install requirements-dev.txt or put clang-format 19.1.7 on PATH")
    version = subprocess.check_output([formatter, "--version"], text=True)
    if "19.1.7" not in version:
        raise SystemExit("Use clang-format 19.1.7 to match CI")
    sources = sorted(
        path
        for path in (ROOT / "avboit").rglob("*")
        if path.suffix in (".cpp", ".h", ".inl", ".hlsl", ".hlsli")
    )
    failed = []
    for source in sources:
        # HLSL semantics and attributes are retained; include order is intentional.
        original = source.read_bytes()
        formatted = subprocess.check_output(
            [formatter, "--style=file", "--assume-filename=" + str(source.with_suffix(".cpp"))],
            input=original,
            cwd=ROOT,
        )
        if original != formatted:
            if args.check:
                failed.append(str(source.relative_to(ROOT)))
            else:
                source.write_bytes(formatted)
    if failed:
        raise SystemExit("Formatting needed:\n" + "\n".join(failed))
    command = [
        sys.executable,
        "-m",
        "black",
        "--workers",
        "1",
        "build_native.py",
        "tools",
        "avboit/tests",
    ]
    if args.check:
        command.append("--check")
    subprocess.run(command, cwd=ROOT, check=True)
    print(f"Checked {len(sources)} C++/HLSL sources")


if __name__ == "__main__":
    main()
