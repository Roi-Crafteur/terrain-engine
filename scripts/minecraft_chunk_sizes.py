#!/usr/bin/env python3
"""Sizes of full chunks stored the way Minecraft stores them, to compare with the modifications
by sections of phase 15.

terrain_modification_sizes writes, for each case, the chunks it modifies in full: per chunk a
u32 size (little-endian), then the block states of its sections (palette, then 4 bits per block
when the section holds rock and air). Minecraft compresses each chunk with zlib (level 6 by
default) and stores it in its region file (.mca) after a 5-byte header, in 4096-byte sectors.

usage: python3 scripts/minecraft_chunk_sizes.py <file.chunks>...
"""

import struct
import sys
import zlib

SECTOR = 4096
HEADER = 5


def chunk_payloads(path):
    with open(path, "rb") as stream:
        data = stream.read()
    offset = 0
    while offset < len(data):
        (size,) = struct.unpack_from("<I", data, offset)
        offset += 4
        yield data[offset : offset + size]
        offset += size


def measure(path):
    chunks = raw = compressed = sectors = 0
    for payload in chunk_payloads(path):
        packed = len(zlib.compress(payload, 6))
        chunks += 1
        raw += len(payload)
        compressed += packed
        sectors += -(-(packed + HEADER) // SECTOR) * SECTOR
    print(
        f"{path}: {chunks} chunks, {raw} bytes of block states, {compressed} bytes with zlib, "
        f"{sectors} bytes in sectors of 4 KiB"
    )


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 1
    for path in sys.argv[1:]:
        measure(path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
