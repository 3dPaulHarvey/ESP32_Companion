#!/usr/bin/env python3
# Asset table format derived from Espressif esp_mmap_assets (Apache-2.0).
# SPDX-FileCopyrightText: 2022-2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Apache-2.0
"""Pack only wn9_hiesp using the existing ESP-SR and flash asset formats.

The partition stays at 0x600000 with a 2 MB budget. Fonts, emoji, downloaded
assets and alternate wake models are not part of this hardware's asset image.
"""

import argparse
import json
from pathlib import Path
import struct


def fixed_name(name):
    encoded = name.encode("ascii")
    if len(encoded) >= 32:
        raise ValueError(f"Asset name must fit 31 ASCII bytes: {name}")
    return encoded.ljust(32, b"\0")


def pack_model(model_dir):
    if model_dir.name != "wn9_hiesp":
        raise ValueError("Only the verified wn9_hiesp model is supported")
    files = sorted(path for path in model_dir.iterdir() if path.is_file())
    if {p.name for p in files} != {"wn9_data", "wn9_index", "_MODEL_INFO_"}:
        raise ValueError("The Hi ESP model must contain its data, index and model info")
    offset = 4 + 36 + 40 * len(files)
    header = bytearray(struct.pack("<I32sI", 1, fixed_name(model_dir.name), len(files)))
    data = bytearray()
    for path in files:
        payload = path.read_bytes()
        if not payload:
            raise ValueError(f"Empty wake model file: {path.name}")
        header.extend(struct.pack("<32sII", fixed_name(path.name), offset + len(data), len(payload)))
        data.extend(payload)
    return bytes(header + data)


def pack_assets(model):
    # Each table offset points at a two-byte 0x5A prefix before the file data.
    files = [("srmodels.bin", model),
             ("index.json", json.dumps({"version": 1, "srmodels": "srmodels.bin"}).encode("utf-8"))]
    table, data = bytearray(), bytearray()
    for name, payload in files:
        table.extend(struct.pack("<32sIIHH", fixed_name(name), len(payload), len(data), 0, 0))
        data.extend(b"\x5a\x5a" + payload)
    body = table + data
    image = struct.pack("<III", len(files), sum(body) & 0xFFFF, len(body)) + body
    if len(image) > 0x200000:
        raise ValueError("Wake assets exceed the fixed 2 MB partition")
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    image = pack_assets(pack_model(args.model_dir))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(f"Packed wn9_hiesp: {len(image)} bytes -> {args.output}")


if __name__ == "__main__":
    main()
