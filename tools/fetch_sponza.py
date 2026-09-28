# SPDX-License-Identifier: MIT
"""Install only pinned Sponza files; stdlib-only, resumable, no Git/LFS required."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import tempfile
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]


def checked_path(root, relative):
    path = PurePosixPath(relative)
    if path.is_absolute() or ".." in path.parts or "\\" in relative or ":" in relative:
        raise ValueError(f"Unsafe manifest path: {relative}")
    result = root.joinpath(*path.parts).resolve()
    if not result.is_relative_to(root.resolve()):
        raise ValueError(f"Path escapes asset directory: {relative}")
    return result


def matches(data, item):
    if len(data) != item["size"]:
        return False
    # Git's blob identifier identifies the exact file from the pinned tree.
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest() == item["sha"]


def install(destination, lock, opener, cache=None, verify=False):
    repository = lock["repository"].removeprefix("https://github.com/")
    if repository != "KhronosGroup/glTF-Sample-Assets":
        raise ValueError("Unexpected asset repository")
    revision = lock["revision"]
    if len(revision) != 40 or any(c not in "0123456789abcdef" for c in revision):
        raise ValueError("Asset revision must be a full commit SHA")
    installed = cached = downloaded = 0
    for item in lock["files"]:
        target = checked_path(destination, item["path"])
        if target.is_file() and matches(target.read_bytes(), item):
            installed += 1
            continue
        if verify:
            raise RuntimeError(f"Missing or mismatched asset: {target}")
        data = None
        if cache:
            # An existing browser asset cache may use a flat layout.
            for candidate in (checked_path(cache, item["path"]), cache / Path(item["path"]).name):
                if candidate.is_file():
                    candidate_data = candidate.read_bytes()
                    if matches(candidate_data, item):
                        data = candidate_data
                        cached += 1
                        break
        if data is None:
            url = (
                f"https://raw.githubusercontent.com/{repository}/{revision}/"
                + urllib.parse.quote(item["path"])
            )
            with opener.open(url, timeout=60) as response:
                data = response.read(item["size"] + 1)
            if not matches(data, item):
                raise RuntimeError(f'Asset integrity check failed: {item["path"]}')
            downloaded += 1
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = None
        try:
            with tempfile.NamedTemporaryFile(dir=target.parent, delete=False) as output:
                temporary = Path(output.name)
                output.write(data)
            os.replace(temporary, target)
        finally:
            if temporary and temporary.exists():
                temporary.unlink()
        print(f'OK {item["path"]}', flush=True)
    print(
        f'Validated {len(lock["files"])} files: {installed} existing, {cached} cached, {downloaded} downloaded.'
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, default=ROOT / "assets/gltf-sample-assets")
    parser.add_argument(
        "--cache-dir", type=Path, help="Reuse matching local files before downloading"
    )
    parser.add_argument("--proxy", help="Per-run HTTP(S) proxy; never changes global settings")
    parser.add_argument(
        "--verify", action="store_true", help="Verify installed files without any network access"
    )
    args = parser.parse_args()
    lock = json.loads((ROOT / "assets/sponza.lock.json").read_text(encoding="utf-8"))
    handlers = (
        [urllib.request.ProxyHandler({"http": args.proxy, "https": args.proxy})]
        if args.proxy
        else []
    )
    install(
        args.destination.resolve(),
        lock,
        urllib.request.build_opener(*handlers),
        args.cache_dir.resolve() if args.cache_dir else None,
        args.verify,
    )


if __name__ == "__main__":
    main()
