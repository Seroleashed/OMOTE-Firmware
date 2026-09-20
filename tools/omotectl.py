#!/usr/bin/env -S uv run --script
# /// script
# requires-python = ">=3.11"
# dependencies = ["pyserial>=3.5"]
# ///
"""Move configuration files on and off an OMOTE.

    uv run tools/omotectl.py --port /dev/ttyUSB0 list
    uv run tools/omotectl.py --port /dev/ttyUSB0 pull                # everything
    uv run tools/omotectl.py --port /dev/ttyUSB0 pull /cfg/system.json
    uv run tools/omotectl.py --port /dev/ttyUSB0 push devices_library/lgTV.json
    uv run tools/omotectl.py --port /dev/ttyUSB0 rm /cfg/devices/lgTV.json
    uv run tools/omotectl.py --port /dev/ttyUSB0 info

The simulator speaks the same protocol over a socket on localhost, which is how
any of this gets tried out while no hardware exists:

    pio run -e linux_64bit && ./.pio/build/linux_64bit/program &
    uv run tools/omotectl.py --tcp 127.0.0.1:8377 list

`push` works out where a file belongs from its type: a device pack goes to
/cfg/devices/<id>.json, everything else to its fixed place. Pass --to to
override.
"""

from __future__ import annotations

import argparse
import binascii
import json
import socket
import sys
import time
from pathlib import Path

MAGIC = "OMOTE-CONFIG-V1"
DEFAULT_TIMEOUT = 5.0

# where a file belongs, by what it says it is
PATH_BY_TYPE = {
    "omote.system": "/cfg/system.json",
    "omote.scenes": "/cfg/scenes.json",
    "omote.keys": "/cfg/keys.json",
    "omote.ui": "/cfg/ui.json",
}


class ProtocolError(RuntimeError):
    pass


class Link:
    """A byte stream to the device, serial or TCP. The protocol is the same."""

    def __init__(self, stream, timeout: float = DEFAULT_TIMEOUT):
        self._stream = stream
        self._timeout = timeout
        self._buffer = b""

    @classmethod
    def open_serial(cls, port: str, baud: int, timeout: float):
        import serial  # imported here so --tcp works without pyserial present

        handle = serial.Serial(port, baud, timeout=0.1)
        # Opening a serial port resets most ESP32 boards. Give the firmware time
        # to come up, or the magic line is sent into a bootloader.
        time.sleep(2.0)
        handle.reset_input_buffer()
        return cls(_SerialStream(handle), timeout)

    @classmethod
    def open_tcp(cls, host: str, port: int, timeout: float):
        handle = socket.create_connection((host, port), timeout=timeout)
        handle.settimeout(0.1)
        return cls(_SocketStream(handle), timeout)

    # --- raw bytes -----------------------------------------------------------
    def write(self, data: bytes) -> None:
        self._stream.write(data)

    def _fill(self) -> bool:
        chunk = self._stream.read()
        if chunk:
            self._buffer += chunk
            return True
        return False

    def read_line(self) -> str:
        deadline = time.monotonic() + self._timeout
        while b"\n" not in self._buffer:
            if time.monotonic() > deadline:
                raise ProtocolError(
                    f"no answer within {self._timeout}s (have: {self._buffer[:80]!r})"
                )
            self._fill()
        line, _, rest = self._buffer.partition(b"\n")
        self._buffer = rest
        return line.decode("utf-8", errors="replace").rstrip("\r")

    def read_exactly(self, count: int) -> bytes:
        deadline = time.monotonic() + self._timeout
        while len(self._buffer) < count:
            if time.monotonic() > deadline:
                raise ProtocolError(f"expected {count} bytes, got {len(self._buffer)}")
            if self._fill():
                deadline = time.monotonic() + self._timeout
        data, self._buffer = self._buffer[:count], self._buffer[count:]
        return data

    # --- protocol ------------------------------------------------------------
    def start_session(self) -> None:
        # The leading newline finishes whatever half a line the device may have
        # in its buffer - noise on the wire while the board was booting, or a
        # keystroke from somebody who had a terminal open. Without it the magic
        # word gets appended to that leftover and is not recognised.
        self.write(("\n" + MAGIC + "\n").encode())
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            line = self.read_line()
            if line.startswith(MAGIC):
                return
            # Anything else is a log line the firmware wrote before it went
            # quiet. Skipping them is expected, not an error.
        raise ProtocolError("the device did not open a session")

    def end_session(self) -> None:
        try:
            self.write(b"BYE\n")
            self.read_line()
        except (ProtocolError, OSError):
            pass  # closing down, nothing left to salvage

    def command(self, line: str) -> str:
        self.write((line + "\n").encode())
        answer = self.read_line()
        if answer.startswith("ERR "):
            raise ProtocolError(answer[4:])
        return answer

    def close(self) -> None:
        self._stream.close()


class _SerialStream:
    def __init__(self, handle):
        self._handle = handle

    def read(self) -> bytes:
        return self._handle.read(4096)

    def write(self, data: bytes) -> None:
        self._handle.write(data)
        self._handle.flush()

    def close(self) -> None:
        self._handle.close()


class _SocketStream:
    def __init__(self, handle):
        self._handle = handle

    def read(self) -> bytes:
        try:
            return self._handle.recv(4096)
        except (TimeoutError, socket.timeout):
            return b""

    def write(self, data: bytes) -> None:
        self._handle.sendall(data)

    def close(self) -> None:
        self._handle.close()


# --- the commands ------------------------------------------------------------


def do_list(link: Link) -> list[tuple[str, int]]:
    answer = link.command("LIST")
    count = int(answer.split()[1])
    files = []
    for _ in range(count):
        path, size = link.read_line().rsplit(" ", 1)
        files.append((path, int(size)))
    link.read_line()  # END
    return files


def do_get(link: Link, path: str) -> bytes:
    answer = link.command(f"GET {path}")
    parts = answer.split()
    length, expected_crc = int(parts[1]), int(parts[2], 16)

    payload = link.read_exactly(length)
    link.read_line()  # the newline the device adds, or the rest of it
    while True:
        line = link.read_line()
        if line == "END":
            break

    actual_crc = binascii.crc32(payload) & 0xFFFFFFFF
    if actual_crc != expected_crc:
        raise ProtocolError(
            f"{path} arrived damaged: crc {actual_crc:08x}, expected {expected_crc:08x}"
        )
    return payload


def do_put(link: Link, path: str, payload: bytes) -> None:
    crc = binascii.crc32(payload) & 0xFFFFFFFF
    answer = link.command(f"PUT {path} {len(payload)} {crc:08x}")
    if not answer.startswith("READY") and not answer.startswith("OK"):
        raise ProtocolError(f"unexpected answer: {answer}")

    # In chunks, waiting for the device to say how far it got. The device is
    # slower than the host and its buffer is small.
    sent = 0
    chunk_size = 256
    while sent < len(payload):
        piece = payload[sent : sent + chunk_size]
        link.write(piece)
        sent += len(piece)
        if sent < len(payload):
            link.read_line()  # ACK

    final = link.read_line()
    if final.startswith("ERR "):
        raise ProtocolError(final[4:])


def target_path_for(payload: bytes, override: str | None) -> str:
    if override:
        return override
    try:
        document = json.loads(payload)
    except json.JSONDecodeError as error:
        raise SystemExit(f"not valid JSON, and no --to given: {error}") from error

    file_type = document.get("type")
    if file_type == "omote.devicePack":
        device_id = document.get("device", {}).get("id")
        if not device_id:
            raise SystemExit("a device pack without device.id - cannot tell where it goes")
        return f"/cfg/devices/{device_id}.json"
    if file_type in PATH_BY_TYPE:
        return PATH_BY_TYPE[file_type]
    raise SystemExit(f"unknown file type {file_type!r}, pass --to")


# --- entry point --------------------------------------------------------------


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--port", help="serial port, e.g. /dev/ttyUSB0")
    where.add_argument("--tcp", help="host:port of a running simulator")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT)
    parser.add_argument("--dir", default="cfg_backup", help="where pull writes to")

    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("list")
    commands.add_parser("info")
    pull = commands.add_parser("pull")
    pull.add_argument("paths", nargs="*", help="default: everything")
    push = commands.add_parser("push")
    push.add_argument("files", nargs="+")
    push.add_argument("--to", help="override the target path")
    remove = commands.add_parser("rm")
    remove.add_argument("paths", nargs="+")
    return parser


def main() -> int:
    arguments = build_parser().parse_args()

    if arguments.tcp:
        host, _, port = arguments.tcp.partition(":")
        link = Link.open_tcp(host, int(port or 8377), arguments.timeout)
    else:
        link = Link.open_serial(arguments.port, arguments.baud, arguments.timeout)

    try:
        link.start_session()

        if arguments.command == "list":
            for path, size in do_list(link):
                print(f"{size:8d}  {path}")

        elif arguments.command == "info":
            link.command("INFO")
            while True:
                line = link.read_line()
                if line == "END":
                    break
                print(line)

        elif arguments.command == "pull":
            paths = arguments.paths or [path for path, _ in do_list(link)]
            target_dir = Path(arguments.dir)
            for path in paths:
                payload = do_get(link, path)
                destination = target_dir / path.lstrip("/")
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_bytes(payload)
                print(f"{len(payload):8d}  {path} -> {destination}")

        elif arguments.command == "push":
            for name in arguments.files:
                payload = Path(name).read_bytes()
                path = target_path_for(payload, arguments.to)
                do_put(link, path, payload)
                print(f"{len(payload):8d}  {name} -> {path}")
            print("pushed. The device loads it on the next start - `rm`/`push` again, then REBOOT.")

        elif arguments.command == "rm":
            for path in arguments.paths:
                link.command(f"DEL {path}")
                print(f"deleted  {path}")

        link.end_session()
        return 0

    except ProtocolError as error:
        print(f"error: {error}", file=sys.stderr)
        link.end_session()
        return 1
    finally:
        link.close()


if __name__ == "__main__":
    sys.exit(main())
