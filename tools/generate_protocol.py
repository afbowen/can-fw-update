#! /usr/bin/env python3

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
config = json.loads((ROOT / "protocol.json").read_text())

def fmt(value):
    if isinstance(value, str):
        return f"0x{int(value, 0):X}"
    return str(value)



##############
# C++ Header #
##############

cpp = [
    "// Auto-generated. Do not edit.",
    "#pragma once",
    "#include <stdint.h>",
    "",
    "namespace CanFwUpdateProtocol {",
]

for name, value in config["can_ids"].items():
    cpp.append(f"constexpr uint16_t {name} = {fmt(value)};")

cpp.append(f"constexpr uint16_t BLOCK_SIZE = {config['block_size']};")

for section, name in [("commands", "Command"), ("statuses", "Status")]:
    cpp += ["", f"enum class {name} : uint8_t {{"]
    for key, value in config[section].items():
        cpp.append(f"    {key} = {fmt(value)},")
    cpp.append("};")

cpp.append("} // namespace CanFwUpdateProtocol")

(ROOT / "src/CanFwUpdateProtocol.h").write_text("\n".join(cpp) + "\n")



#################
# Python Module #
#################

py = [
    "# Auto-generated. Do not edit.",
    "from enum import IntEnum",
    "",
]

for name, value in config["can_ids"].items():
    py.append(f"{name} = {fmt(value)}")

py.append(f"BLOCK_SIZE = {config['block_size']}")

for section, name in [("commands", "Command"), ("statuses", "Status")]:
    py += ["", f"class {name}(IntEnum):"]
    for key, value in config[section].items():
        py.append(f"    {key} = {fmt(value)}")

(ROOT / "host/can_fw_update_protocol.py").write_text("\n".join(py) + "\n")
