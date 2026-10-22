#!/usr/bin/env python3
"""Send an .nds to DSi Link over Wi-Fi. Python 3.8+, standard library only.

DSi Link saves the file to Homebrew/Dev/Received on the card; start it from Pico
Launcher. --run also asks DSi Link to boot it through Pico Loader, which is
experimental and does not work yet. Wire format: source/link_protocol.h.
"""
import argparse
import os
import re
import socket
import struct
import sys
import zlib

PORT = 17494
MAX_SIZE = 32 * 1024 * 1024
NAME_PATTERN = re.compile(r"^[A-Za-z0-9_\-][A-Za-z0-9 ._\-]{0,58}\.[Nn][Dd][Ss]$")


def read_reply(sock):
    line = bytearray()
    while len(line) <= 200:
        byte = sock.recv(1)
        if not byte or byte == b"\n":
            break
        line += byte
    return line.decode("ascii", "replace")


def send(address, path, name=None, port=PORT, run=False, timeout=15.0, out=sys.stdout):
    name = name or os.path.basename(path)
    if not NAME_PATTERN.match(name) or ".." in name:
        raise ValueError(f"name {name!r} must be a plain .nds file name "
                         "(letters, digits, space, dot, dash, underscore; up to 63 characters)")
    with open(path, "rb") as f:
        data = f.read()
    if not 0 < len(data) <= MAX_SIZE:
        raise ValueError(f"file size {len(data)} is outside 1 byte to 32 MiB")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    encoded = name.encode("ascii")
    header = b"DSLK" + struct.pack("<IIIHH", 1 if run else 0, len(data), crc, len(encoded), 0)
    try:
        sock = socket.create_connection((address, port), timeout=timeout)
    except OSError as error:
        raise ConnectionError(f"no answer from {address}:{port}; open DSi Link on the DSi "
                              f"and check the address it shows ({error})") from error
    with sock:
        sock.settimeout(timeout)
        sock.sendall(header + encoded)
        ready = read_reply(sock)
        if ready != "READY":
            raise RuntimeError(f"DSi Link refused the file: {ready}")
        sent = 0
        for offset in range(0, len(data), 16384):
            piece = data[offset:offset + 16384]
            sock.sendall(piece)
            sent += len(piece)
            if out.isatty():
                print(f"\rSending {name}: {100 * sent // len(data)}%", end="", file=out, flush=True)
        if out.isatty():
            print(file=out)
        result = read_reply(sock)
    if not result.startswith("OK"):
        raise RuntimeError(f"DSi Link reported: {result}")
    print(f"{address}: {result} ({len(data)} bytes, CRC-32 {crc:08X})", file=out)
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description="Send an .nds file to DSi Link over Wi-Fi.")
    parser.add_argument("address", help="address DSi Link shows on screen, e.g. 192.168.1.50")
    parser.add_argument("file", help=".nds file to send")
    parser.add_argument("--name", help="save under this file name instead (must end in .nds)")
    parser.add_argument("--port", type=int, default=PORT)
    parser.add_argument("--run", action="store_true", help="also boot it through Pico Loader (experimental, not working yet)")
    parser.add_argument("--no-run", action="store_true", help="save only (the default; kept for older commands)")
    parser.add_argument("--timeout", type=float, default=15.0, help="seconds to wait for the DSi")
    args = parser.parse_args(argv)
    try:
        send(args.address, args.file, args.name, args.port, args.run and not args.no_run, args.timeout)
    except (OSError, ValueError, RuntimeError) as error:
        print(f"dsi_send: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
