Import("env")

import os
import shutil


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
        print(f"Copying firmware.bin to {new_name}.bin")
        shutil.copyfile(original_bin, new_bin)


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", rename_bin)
