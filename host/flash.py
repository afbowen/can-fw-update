#! /usr/bin/env python3

"""Minimal block-based firmware sender over Classical CAN (SocketCAN)."""
import argparse

import struct
import sys
import time
import zlib
from pathlib import Path
import can

from can_fw_update_protocol import CMD_ID_BASE, DATA_ID_BASE, STATUS_ID_BASE, Command, Status, BLOCK_SIZE


class _Progress:
    def __init__(self, total):
        self.total = total
        self.completed = 0
        self.started = time.monotonic()
        self.last_refresh = self.started
        self.width = 0
        self.phase = "Uploading"
        self.refresh(force=True)

    def refresh(self, force=False):
        now = time.monotonic()
        if not force and now - self.last_refresh < 0.1:
            return
        self.last_refresh = now
        elapsed = now - self.started
        fraction = self.completed / self.total if self.total else 0
        filled = int(24 * fraction)
        bar = "#" * filled + "-" * (24 - filled)
        eta = f"{elapsed * (self.total - self.completed) / self.completed:.1f}s" if self.completed else "--"
        line = (
            f"{self.phase} [{bar}] {fraction:6.1%} "
            f"{self.completed}/{self.total} bytes | "
            f"{elapsed:.1f}s elapsed | ETA {eta}"
        )
        print("\r" + line.ljust(self.width), end="", flush=True, file=sys.stderr)
        self.width = len(line)

    def advance(self, completed):
        self.completed = completed
        self.refresh(force=True)

    def message(self, message):
        print("\r" + " " * self.width + "\r" + message, file=sys.stderr)
        self.width = 0
        self.refresh(force=True)

    def finish(self, phase):
        self.phase = phase
        self.refresh(force=True)
        print(file=sys.stderr, flush=True)


def flash(bus, node: int, image: bytes, tries: int = 5) -> None:
    cmd_id = CMD_ID_BASE + node
    data_id = DATA_ID_BASE + node
    reply_id = STATUS_ID_BASE + node

    def send(arbitration_id, data):
        bus.send(can.Message(arbitration_id=arbitration_id, is_extended_id=False, data=data), timeout=2)

    def wait_for(cmd, timeout=3):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            progress.refresh()
            msg = bus.recv(timeout=min(0.1, max(0, deadline - time.monotonic())))
            if not msg or msg.is_extended_id or msg.arbitration_id != reply_id:
                continue
            if len(msg.data) == 6 and msg.data[0] == cmd:
                return msg.data[1], struct.unpack_from('<I', msg.data, 2)[0]
        raise TimeoutError(f"No reply 0x{cmd:02x}")

    # Remove old status frames, if any.
    while bus.recv(timeout=0) is not None:
        pass

    progress = _Progress(len(image))
    try:
        send(cmd_id, struct.pack('<BI', Command.BEGIN, len(image)))
        err, offset = wait_for(Command.BEGIN, 10)
        if err != 0 or offset != 0:
            raise RuntimeError(f"BEGIN failed: error={err} offset={offset}")

        for offset in range(0, len(image), BLOCK_SIZE):
            chunk = image[offset:offset + BLOCK_SIZE]
            for attempt in range(1, tries + 1):
                send(cmd_id, struct.pack('<BIH', Command.BLOCK, offset, len(chunk)))
                try:
                    err, current = wait_for(Command.BLOCK)
                    if err == Status.BAD_STATE and offset > 0 and current == 0:
                        raise RuntimeError("Target rebooted; restart update from byte 0")
                    if err == Status.BAD_OFFSET and current == offset + len(chunk):  # Previous block ACK was lost.
                        break
                    if err != Status.OK or current != offset:
                        raise RuntimeError(f"BLOCK rejected: error={err}, offset={current}")
                    for seq, start in enumerate(range(0, len(chunk), 7)):
                        send(data_id, bytes([seq]) + chunk[start:start + 7])
                        time.sleep(0.00001)  # 1 us between CAN frames (TODO make this dynamic based on baud of interface)
                    err, current = wait_for(Command.DATA, 10)
                    if err == 0 and current == offset + len(chunk):
                        break
                    progress.message(f"Block {offset} failed (error={err}, offset={current})")
                except TimeoutError:
                    progress.message(f"Block {offset}: retry {attempt}/{tries}")
            else:
                raise RuntimeError(f"Block {offset} failed after {tries} attempts")
            progress.advance(offset + len(chunk))

        progress.phase = "Finalizing..."
        progress.refresh(force=True)
        send(cmd_id, struct.pack('<BI', Command.END, zlib.crc32(image)))
        err, current = wait_for(Command.END, 15)

        if err or current != len(image):
            raise RuntimeError(f"END failed: error={err}, offset={current}")

        progress.finish("Complete")
        print("Success; device rebooting into updated firmware")

    except (Exception, KeyboardInterrupt) as exc:
        progress.finish("Cancelled" if isinstance(exc, KeyboardInterrupt) else "Failed")

        if not isinstance(exc, can.CanError):
            try:
                send(cmd_id, bytes([Command.ABORT]))
            except can.CanError:
                pass

        raise

def main():
    p = argparse.ArgumentParser(description="ESP32-C3 firmware update over CAN")
    p.add_argument("firmware", type=Path, help="Compiled Arduino .ino.bin")
    p.add_argument("--channel", default="can0", help="Linux SocketCAN interface")
    p.add_argument("--node", type=int, default=1, help="Node ID 1..31")
    args = p.parse_args()
    if not 1 <= args.node <= 31:
        p.error("--node must be in 1..31")
    image = args.firmware.read_bytes()
    if not image:
        p.error("Firmware file is empty")
    with can.Bus(interface="socketcan", channel=args.channel) as bus:
        flash(bus, args.node, image)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("Update cancelled", file=sys.stderr)
        sys.exit(130)
    except can.CanError as exc:
        print(f"CAN communication failed: {exc}", file=sys.stderr)
        sys.exit(1)
    except (TimeoutError, RuntimeError) as exc:
        print(f"Update failed: {exc}", file=sys.stderr)
        sys.exit(1)
