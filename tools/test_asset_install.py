# SPDX-License-Identifier: MIT
"""Offline tests for integrity, resumability and manifest path containment."""
import hashlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock
from fetch_sponza import checked_path, install


class AssetInstallTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.payload = b"asset contents"
        self.item = {
            "path": "Models/Sponza/glTF/test.bin",
            "size": len(self.payload),
            "sha": hashlib.sha1(b"blob 14\0" + self.payload).hexdigest(),
        }
        self.lock = {
            "repository": "https://github.com/KhronosGroup/glTF-Sample-Assets",
            "revision": "a" * 40,
            "files": [self.item],
        }

    def test_download_and_offline_resume(self):
        opener = Mock()
        opener.open.return_value = io.BytesIO(self.payload)
        install(self.root, self.lock, opener)
        opener.open.assert_called_once()
        opener.open.reset_mock()
        install(self.root, self.lock, opener, verify=True)
        opener.open.assert_not_called()

    def test_bad_download_does_not_replace_existing_file(self):
        target = checked_path(self.root, self.item["path"])
        target.parent.mkdir(parents=True)
        target.write_bytes(b"old file")
        opener = Mock()
        opener.open.return_value = io.BytesIO(b"corrupt payload")
        with self.assertRaises(RuntimeError):
            install(self.root, self.lock, opener)
        self.assertEqual(target.read_bytes(), b"old file")

    def test_flat_cache(self):
        cache = self.root / "cache"
        cache.mkdir()
        (cache / "test.bin").write_bytes(self.payload)
        opener = Mock()
        install(self.root / "assets", self.lock, opener, cache=cache)
        opener.open.assert_not_called()

    def test_offline_missing_fails_without_network(self):
        opener = Mock()
        with self.assertRaises(RuntimeError):
            install(self.root, self.lock, opener, verify=True)
        opener.open.assert_not_called()

    def test_paths_cannot_escape_destination(self):
        for name in ("../escape", "/absolute", "C:/drive", "Models/../../../escape", "a\\b"):
            with self.subTest(name=name), self.assertRaises(ValueError):
                checked_path(self.root, name)


if __name__ == "__main__":
    unittest.main()
