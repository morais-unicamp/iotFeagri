Import("env")

import os
import shutil
import hashlib
import json
import re
from pathlib import Path


def default_profile(env_name):
    normalized = env_name.lower()
    if "c3" in normalized:
        return "exemploESP32C3"
    return "exemploESP32"


def output_name(env):
    explicit_group = os.environ.get("IOTFEAGRI_OTA_GROUP", "").strip()
    if explicit_group:
        return explicit_group

    user = os.environ.get("IOTFEAGRI_OTA_USER", "leandro").strip()
    profile = os.environ.get("IOTFEAGRI_OTA_PROFILE", "").strip()
    if not profile:
        profile = default_profile(env.subst("$PIOENV"))

    return f"{user}_{profile}"


def rename_bin(source, target, env):
    build_dir = env.subst("$BUILD_DIR")
    original_bin = os.path.join(build_dir, "firmware.bin")
    new_name = output_name(env)
    new_bin = os.path.join(build_dir, f"{new_name}.bin")

    if os.path.exists(original_bin):
        header = Path(env.subst("$PROJECT_DIR")) / "include" / "firmware_version.h"
        match = re.search(r'^#define\s+IOTFEAGRI_EXAMPLE_FW_VERSION\s+"([^"]+)"',
                          header.read_text(encoding="utf-8"), re.MULTILINE)
        if not match:
            raise ValueError("Missing IOTFEAGRI_EXAMPLE_FW_VERSION in firmware_version.h")
        print(f"Copying firmware.bin to {new_name}.bin")
        shutil.copyfile(original_bin, new_bin)
        base_url = os.environ.get(
            "IOTFEAGRI_OTA_BASE_URL",
            "https://leandro144.feagri.unicamp.br/static/firmware",
        ).rstrip("/")
        manifest = {
            "url": f"{base_url}/{new_name}/firmware.bin",
            "md5": hashlib.md5(Path(new_bin).read_bytes()).hexdigest(),
            "version": match.group(1),
            "group": new_name,
        }
        manifest_path = Path(build_dir) / f"{new_name}.manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        print(f"OTA manifest: {manifest_path.name} (version {manifest['version']})")


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_bin)
