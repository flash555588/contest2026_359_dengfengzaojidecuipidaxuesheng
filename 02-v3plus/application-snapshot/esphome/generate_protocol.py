#!/usr/bin/env python3
"""Generate the supported API constants from the pinned official api.proto."""

from pathlib import Path
import argparse
import hashlib
import re

MESSAGES = (
    "HelloRequest", "HelloResponse", "AuthenticationRequest",
    "AuthenticationResponse", "DisconnectRequest", "DisconnectResponse",
    "PingRequest", "PingResponse", "DeviceInfoRequest", "DeviceInfoResponse",
    "ListEntitiesRequest", "ListEntitiesDoneResponse", "SubscribeStatesRequest",
    "ListEntitiesBinarySensorResponse", "BinarySensorStateResponse",
    "ListEntitiesSensorResponse", "SensorStateResponse",
    "ListEntitiesSwitchResponse", "SwitchStateResponse", "SwitchCommandRequest",
    "ListEntitiesLightResponse", "LightStateResponse", "LightCommandRequest",
    "ListEntitiesTextSensorResponse", "TextSensorStateResponse",
)


def generate(source: Path) -> str:
    """Extract message and field IDs without importing ESPHome build tooling."""
    raw = source.read_bytes()
    text = raw.decode("utf-8")
    lines = [
        "/* SPDX-License-Identifier: MIT",
        " * Generated from ESPHome api.proto; do not edit field numbers.",
        " * Source: esphome/esphome@e3dd2f44a45bc7200566393db51fa17eb0a5edf1",
        f" * SHA256: {hashlib.sha256(raw).hexdigest()}",
        " * Copyright (c) 2019 ESPHome. See THIRD_PARTY.md.",
        " */",
        "#ifndef OPENVELA_ESPHOME_PROTOCOL_IDS_H",
        "#define OPENVELA_ESPHOME_PROTOCOL_IDS_H",
    ]
    for name in MESSAGES:
        match = re.search(r"^message " + name + r" \{(.*?)^\}", text, re.M | re.S)
        if match is None:
            raise ValueError(f"Missing official message: {name}")
        body = re.sub(r"//[^\n]*", "", match[1])
        msg_id = re.search(r"option\s+\(id\)\s*=\s*(\d+)\s*;", body)
        if msg_id is None:
            raise ValueError(f"Missing official ID: {name}")
        lines.append(f"#define EH_{name} {msg_id[1]}u")
        for field, value in re.findall(
            r"^\s*(?:repeated\s+)?\w+\s+(\w+)\s*=\s*(\d+)\b", body, re.M
        ):
            lines.append(f"#define EH_{name}_{field} {value}u")
    lines.extend(["#endif", ""])
    return "\n".join(lines)


if __name__ == "__main__":
    root = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--proto", type=Path, default=root / "reference/api.proto")
    args = parser.parse_args()
    output = root / "protocol_ids.h"
    expected = generate(args.proto)
    if args.check:
        if output.read_text(encoding="utf-8") != expected:
            raise SystemExit("protocol_ids.h differs; rerun generate_protocol.py")
    else:
        output.write_text(expected, encoding="utf-8", newline="\n")
