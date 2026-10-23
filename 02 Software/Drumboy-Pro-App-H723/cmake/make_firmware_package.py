#!/usr/bin/env python3
"""Wrap an application .bin into an SD-card update package for the bootloader.

Same format as 02 Software/Firmware/Drumboy-Pro-Firmware-Builder.cpp:

    "RW_DRUMBOY_PRO_FIRMWARE "  (24 bytes)
    versionMajor  u16 LE
    versionMinor  u16 LE
    payload size  u32 LE
    payload CRC32 u32 LE        (standard zlib CRC-32)
    payload

Copy the output to the SD card root; the bootloader picks up any file named
Drumboy_Pro_Fw?*.bin on the next power-up.
"""

import argparse
import struct
import zlib

HEADER = b"RW_DRUMBOY_PRO_FIRMWARE "
MAX_PAYLOAD = 917504  # Limit enforced by the bootloader (sectors 1-7)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", help="application .bin produced by objcopy")
    parser.add_argument("target", help="output package, e.g. Drumboy_Pro_Fw_01_01.bin")
    parser.add_argument("--major", type=int, default=1, help="version major (0-9)")
    parser.add_argument("--minor", type=int, default=1, help="version minor (0-99)")
    args = parser.parse_args()

    if not (0 <= args.major < 10 and 0 <= args.minor < 100):
        parser.error("bootloader requires major < 10 and minor < 100")

    with open(args.source, "rb") as f:
        payload = f.read()

    if not 0 < len(payload) < MAX_PAYLOAD:
        parser.error(f"payload size {len(payload)} outside bootloader limit (1..{MAX_PAYLOAD - 1})")

    crc = zlib.crc32(payload) & 0xFFFFFFFF
    with open(args.target, "wb") as f:
        f.write(HEADER)
        f.write(struct.pack("<HHII", args.major, args.minor, len(payload), crc))
        f.write(payload)

    print(f"{args.target}: v{args.major}.{args.minor:02d}, {len(payload)} bytes, crc32 {crc:08x}")


if __name__ == "__main__":
    main()
