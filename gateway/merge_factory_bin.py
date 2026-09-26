# PlatformIO post-build script: merge bootloader + partitions + app into
# firmware.factory.bin (flashable at 0x0 with esptool, per firmware-release/).

Import("env")  # noqa: F821

import subprocess
import sys
from pathlib import Path


def merge_factory_bin(source, target, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    esptool = Path(env.PioPlatform().get_package_dir("tool-esptoolpy")) / "esptool.py"
    out = build_dir / "firmware.factory.bin"

    images = [
        (0x0, "bootloader.bin"),
        (0x8000, "partitions.bin"),
        (0xF000, "ota_data_initial.bin"),
        (0x20000, "firmware.bin"),  # partitions.csv: ota_0
    ]

    cmd = [sys.executable, str(esptool), "--chip", "esp32s3", "merge_bin",
           "-o", str(out), "--flash_mode", "dio", "--flash_freq", "80m",
           "--flash_size", "8MB"]
    for offset, name in images:
        cmd += [hex(offset), str(build_dir / name)]

    subprocess.run(cmd, check=True)
    print(f"merge_factory_bin: {out.name} ready")


env.AddPostAction("$BUILD_DIR/firmware.bin", merge_factory_bin)
