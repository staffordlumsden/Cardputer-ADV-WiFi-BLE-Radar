#!/usr/bin/env python3
"""
Package a compiled native app binary for SD:/apps/<id>/ deployment.
Validates manifest schema, ESP32 image magic byte (0xE9), and ota_0 size limit (5 MiB).
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys

OTA0_MAX_BYTES = 0x500000  # 5 MiB
ESP_IMAGE_MAGIC = 0xE9
VALID_ID_RE = re.compile(r"^[a-z0-9][a-z0-9_-]{0,31}$")


def validate_manifest(data: dict) -> list[str]:
    errors = []
    if data.get("schema_version") != 1:
        errors.append("schema_version must be 1")
    app_id = data.get("id", "")
    if not isinstance(app_id, str) or not VALID_ID_RE.match(app_id):
        errors.append("invalid app id (must match ^[a-z0-9][a-z0-9_-]{0,31}$)")
    if data.get("target") != "cardputer-adv":
        errors.append("target must be 'cardputer-adv'")
    binary = data.get("binary", "")
    if not isinstance(binary, str) or not binary or "/" in binary or "\\" in binary or ".." in binary:
        errors.append("binary must be a safe relative filename")
    if not isinstance(data.get("sdk_version"), int) or data["sdk_version"] < 1:
        errors.append("sdk_version must be >= 1")
    return errors


def validate_app_binary(bin_path: str) -> tuple[int, str]:
    size = os.path.getsize(bin_path)
    if size == 0 or size > OTA0_MAX_BYTES:
        raise ValueError(f"Binary size {size} bytes out of range (1..{OTA0_MAX_BYTES})")
    with open(bin_path, "rb") as f:
        header = f.read(4)
        if len(header) < 1 or header[0] != ESP_IMAGE_MAGIC:
            raise ValueError(f"Invalid ESP image magic byte: 0x{header[0]:02X} (expected 0xE9)")
        f.seek(0)
        sha256 = hashlib.sha256(f.read()).hexdigest()
    return size, sha256


def main() -> int:
    parser = argparse.ArgumentParser(description="Package a Cardputer OS native app")
    parser.add_argument("--manifest", required=True, help="Path to manifest.json")
    parser.add_argument("--binary", required=True, help="Path to compiled firmware.bin")
    parser.add_argument("--out", required=True, help="Output directory (e.g. dist/sd/apps/<id>)")
    args = parser.parse_args()

    with open(args.manifest, "r", encoding="utf-8") as f:
        manifest = json.load(f)

    errors = validate_manifest(manifest)
    if errors:
        print("Manifest validation failed:", "; ".join(errors), file=sys.stderr)
        return 1

    size, sha256 = validate_app_binary(args.binary)
    manifest["size"] = size
    manifest["sha256"] = sha256

    os.makedirs(args.out, exist_ok=True)
    shutil.copy2(args.binary, os.path.join(args.out, manifest["binary"]))
    with open(os.path.join(args.out, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")

    print(f"Packaged {manifest['id']} ({size} bytes, sha256={sha256[:12]}...) -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
