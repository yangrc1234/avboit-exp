# SPDX-License-Identifier: MIT
"""Check the publishable tree for missing notices, artifacts and broken doc links."""

from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def main():
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    paths = sorted({Path(name) for name in result.stdout.decode("utf-8").split("\0") if name})
    errors = []
    required = (
        "LICENSE.txt",
        "THIRD_PARTY_NOTICES.md",
        "README.md",
        "README.zh-CN.md",
        "CONTRIBUTING.md",
        ".gitmodules",
        ".github/workflows/ci.yml",
    )
    for name in required:
        if not (ROOT / name).is_file():
            errors.append("Missing required file: " + name)
    text_extensions = {
        ".md",
        ".txt",
        ".h",
        ".cpp",
        ".inl",
        ".hlsl",
        ".hlsli",
        ".py",
        ".cfg",
        ".yml",
        ".json",
    }
    source_extensions = {".h", ".cpp", ".inl", ".hlsl", ".hlsli", ".py"}
    secret_patterns = [
        r"gh[pousr]_[A-Za-z0-9]{30,}",
        r"github_pat_[A-Za-z0-9_]{40,}",
        r"AKIA[A-Z0-9]{16}",
        r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
    ]
    forbidden_roots = {"bin", "dist", ".venv", "media", "feature_demo", "examples"}
    for relative in paths:
        path = ROOT / relative
        if not path.is_file():
            continue  # Submodule gitlinks are audited by their pinned upstream licenses.
        if (
            relative.parts[0] in forbidden_roots
            or relative.parts[0].startswith("build")
            and len(relative.parts) > 1
        ):
            errors.append("Generated or unused directory: " + relative.as_posix())
        if path.suffix.lower() in {".exe", ".dll", ".pdb", ".obj", ".glb", ".zip", ".ppm", ".raw"}:
            errors.append("Binary/model/capture should not be committed: " + relative.as_posix())
        if path.stat().st_size > 5 * 1024 * 1024:
            errors.append("Unexpected file larger than 5 MiB: " + relative.as_posix())
        if path.suffix not in text_extensions:
            continue
        content = path.read_text(encoding="utf-8")
        if not content.endswith("\n"):
            errors.append("Missing final newline: " + relative.as_posix())
        if any(line.rstrip() != line for line in content.splitlines()):
            errors.append("Trailing whitespace: " + relative.as_posix())
        if path.suffix in source_extensions and "SPDX-License-Identifier: MIT" not in content[:256]:
            errors.append("Missing source license identifier: " + relative.as_posix())
        for pattern in secret_patterns:
            if re.search(pattern, content):
                # Report the path only; never echo a possible credential.
                errors.append("Potential credential: " + relative.as_posix())
                break
        if re.search(r"[A-Za-z]:[/\\]Users[/\\][^/\\\s]+", content):
            errors.append("Personal absolute path: " + relative.as_posix())
        if path.suffix == ".md":
            for target in re.findall(r"\]\(([^)]+)\)", content):
                target = target.split(' "', 1)[0].strip("<>")
                url = urlsplit(target)
                if url.scheme or not url.path:
                    continue
                linked = (path.parent / unquote(url.path)).resolve()
                if not linked.is_relative_to(ROOT) or not linked.exists():
                    errors.append(f"Broken local link in {relative.as_posix()}: {target}")
    license_text = (ROOT / "LICENSE.txt").read_text(encoding="utf-8")
    if "NVIDIA CORPORATION" not in license_text or "avboit-exp" not in license_text:
        errors.append("Root license must retain project and NVIDIA notices")
    if errors:
        print("\n".join(errors))
        return 1
    print(f"Repository checks passed ({len(paths)} first-party entries; dependencies excluded)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
