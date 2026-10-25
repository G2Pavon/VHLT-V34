#!/usr/bin/env python3
"""Write a WAD3 file of solid-colour placeholder textures.

Used by the parity corpus to stand in for textures that no available WAD
provides, so hlcsg can compile maps whose original WADs are lost.

    mkwad.py out.wad NAME [NAME ...] [--size 64] [--rgb 255,255,255]
"""

import argparse
import struct
import sys

MIPLEVELS = 4
TYP_MIPTEX = 0x43


def miptex(name: str, size: int, rgb: tuple[int, int, int]) -> bytes:
    encoded = name.encode("ascii")
    if len(encoded) > 15:
        raise ValueError(f"texture name too long (max 15 chars): {name}")

    header_len = 16 + 4 * 2 + 4 * MIPLEVELS
    offsets = []
    pixels = b""
    ofs = header_len
    for level in range(MIPLEVELS):
        dim = size >> level
        offsets.append(ofs)
        pixels += b"\x00" * (dim * dim)  # every pixel uses palette index 0
        ofs += dim * dim

    palette = bytes(rgb) * 256
    return (
        struct.pack("<16sII4I", encoded, size, size, *offsets)
        + pixels
        + struct.pack("<H", 256)
        + palette
        + b"\x00\x00"
    )


def write_wad(path: str, names: list[str], size: int, rgb: tuple[int, int, int]) -> None:
    write_wad_lumps(path, [(name, miptex(name, size, rgb)) for name in names])


def write_wad_lumps(path: str, entries: list[tuple[str, bytes]]) -> None:
    """A WAD3 of the given (name, miptex data) lumps, in that order (names may repeat)."""
    lumps = []
    body = b""
    ofs = 12
    for name, data in entries:
        lumps.append((ofs, len(data), name))
        body += data
        ofs += len(data)

    directory = b"".join(
        struct.pack("<IIIbbH16s", lofs, length, length, TYP_MIPTEX, 0, 0, name.encode("ascii"))
        for lofs, length, name in lumps
    )
    with open(path, "wb") as f:
        f.write(struct.pack("<4sii", b"WAD3", len(lumps), ofs))
        f.write(body)
        f.write(directory)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out")
    ap.add_argument("names", nargs="+")
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--rgb", default="255,255,255")
    args = ap.parse_args()

    rgb = tuple(int(c) for c in args.rgb.split(","))
    if args.size < 8 or args.size % 8:
        ap.error("--size must be a multiple of 8")
    names = sorted(set(args.names), key=str.upper)
    write_wad(args.out, names, args.size, rgb)
    return 0


if __name__ == "__main__":
    sys.exit(main())
