"""Run with: python -m unittest discover -s tests -v (from repository root)."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
PROJECT = ROOT / "examples" / "PlatformIO"


class BuildEnvironment:
    def __init__(self, project, build, target):
        self.values = {"$PROJECT_DIR": str(project), "$BUILD_DIR": str(build),
                       "$PIOENV": target}

    def subst(self, key):
        return self.values[key]

    def AddPostAction(self, target, callback):
        self.callback = callback


class ReleaseArtifactsTest(unittest.TestCase):
    def build_artifacts(self, directory, target, variables=None):
        build = directory / "build"
        build.mkdir()
        binary = b"firmware-fixture\x00\xff\x16"
        (build / "firmware.bin").write_bytes(binary)
        env = BuildEnvironment(PROJECT, build, target)
        with patch.dict(os.environ, variables or {}, clear=True):
            runpy.run_path(str(PROJECT / "rename_bin.py"),
                           init_globals={"Import": lambda _: None, "env": env})
            env.callback(None, None, env)
        return build, binary

    def test_default_manifests_match_binary_and_compiled_version(self):
        for target, group in (("esp32dev", "leandro_exemploESP32"),
                              ("esp32-c3-devkitm-1", "leandro_exemploESP32C3")):
            with self.subTest(target=target), tempfile.TemporaryDirectory() as directory:
                build, binary = self.build_artifacts(Path(directory), target)
                manifest = json.loads((build / f"{group}.manifest.json").read_text())
                self.assertEqual(manifest, {
                    "version": "1.1.16", "group": group,
                    "md5": hashlib.md5(binary).hexdigest(),
                    "url": f"https://leandro144.feagri.unicamp.br/static/firmware/{group}/firmware.bin",
                })
                self.assertEqual((build / f"{group}.bin").read_bytes(), binary)

    def test_custom_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            build, _ = self.build_artifacts(Path(directory), "esp32dev", {
                "IOTFEAGRI_OTA_GROUP": "ana_estufa",
                "IOTFEAGRI_OTA_BASE_URL": "https://example.org/firmware/",
            })
            manifest = json.loads((build / "ana_estufa.manifest.json").read_text())
            self.assertEqual(manifest["group"], "ana_estufa")
            self.assertEqual(manifest["url"], "https://example.org/firmware/ana_estufa/firmware.bin")

    def test_distributed_headers_and_library_copies_match(self):
        self.assertEqual((PROJECT / "include/firmware_version.h").read_bytes(),
                         (ROOT / "examples/Arduino/firmware_version.h").read_bytes())
        for name in ("iotFeagri.cpp", "iotFeagri.h"):
            for copy in (ROOT / "examples/Arduino", PROJECT / "lib/iotFeagri"):
                self.assertEqual((ROOT / "src" / name).read_bytes(),
                                 (copy / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
