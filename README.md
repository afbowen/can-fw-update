# Firmware Update Over CAN

This repo contains the infrastructure and tooling to facilitate firmware udpates over CAN. There
are two core components:

- MCU Libraries: receives the new firmware binary over CAN, verifies, and installs.
- Python flashing utility: sends the firmware binary to the target device over CAN.

## Supported Platforms
- ESP32-C3 (compiled with Arduino CLI)


## Protocol Definition
The CAN communication protocol is defined by...

- CAN IDs
- Enumerated Commands (Python to MCU)
- Enumerated States (MCU to Python)

Because these values must be known by both the Python updater and the MCU, this definition is
maintained in a common `protocol.json`. When `protocol.json` is changed (and only when it is
changed), we must run the `generate_protocol.py` script to generate...

- `CanFwUpdateProtocol.h` (used by MCU)
- `can_fw_update_protocol.py` (used by Python)

These files should be committed along with `protocol.json` so that tooling works immediately upon
cloning the repo, without needing to generate the protocol.

