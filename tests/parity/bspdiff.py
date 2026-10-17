#!/usr/bin/env python3
"""Structural diff of two GoldSrc BSP v30 files.

Compares the header, every lump record by record, the alignment padding
after each lump, and any bytes not covered by a lump.

    bspdiff.py REF.bsp CAND.bsp [--padding zero-or-equal|zero|strict|ignore] [-q]

Padding policies (the 0-3 bytes AddLump writes after each lump):
  zero-or-equal  candidate padding must be all zero or equal to the reference (default)
  zero           candidate padding must be all zero
  strict         candidate padding must equal the reference
  ignore         padding is not compared

Exit status: 0 identical under the policy, 1 different, 2 unreadable input.
"""

import argparse
import struct
import sys
from dataclasses import dataclass

BSPVERSION = 30
HEADER_LUMPS = 15
HEADER_SIZE = 4 + HEADER_LUMPS * 8

# (name, struct format or None for blobs/text, field names)
LUMPS = [
    ("entities", None, None),
    ("planes", "<4fi", "normal[0] normal[1] normal[2] dist type"),
    ("textures", None, None),
    ("vertexes", "<3f", "x y z"),
    ("visibility", None, None),
    ("nodes", "<i2h3h3hHH", "planenum child0 child1 mins0 mins1 mins2 maxs0 maxs1 maxs2 firstface numfaces"),
    ("texinfo", "<8f2i", "s0 s1 s2 s3 t0 t1 t2 t3 miptex flags"),
    ("faces", "<Hhi2h4Bi", "planenum side firstedge numedges texinfo style0 style1 style2 style3 lightofs"),
    ("lighting", None, None),
    ("clipnodes", "<i2h", "planenum child0 child1"),
    ("leafs", "<2i3h3hHH4B", "contents visofs mins0 mins1 mins2 maxs0 maxs1 maxs2 firstmarksurface nummarksurfaces amb0 amb1 amb2 amb3"),
    ("marksurfaces", "<H", "face"),
    ("edges", "<2H", "v0 v1"),
    ("surfedges", "<i", "edge"),
    ("models", "<9f4i3i", "mins0 mins1 mins2 maxs0 maxs1 maxs2 org0 org1 org2 head0 head1 head2 head3 visleafs firstface numfaces"),
]

MAX_SHOWN = 5


class BspError(Exception):
    pass


@dataclass
class Bsp:
    data: bytes
    version: int
    lumps: list[tuple[int, int]]  # (fileofs, filelen)

    def lump(self, i: int) -> bytes:
        ofs, length = self.lumps[i]
        return self.data[ofs : ofs + length]

    def padding(self, i: int) -> bytes:
        ofs, length = self.lumps[i]
        return self.data[ofs + length : ofs + ((length + 3) & ~3)]


def load(path: str) -> Bsp:
    try:
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        raise BspError(f"{path}: {e.strerror}") from e
    if len(data) < HEADER_SIZE:
        raise BspError(f"{path}: file too small for a BSP header ({len(data)} bytes)")
    version = struct.unpack_from("<i", data, 0)[0]
    lumps = [struct.unpack_from("<ii", data, 4 + 8 * i) for i in range(HEADER_LUMPS)]
    for i, (ofs, length) in enumerate(lumps):
        if ofs < 0 or length < 0 or ofs + length > len(data):
            raise BspError(f"{path}: lump {LUMPS[i][0]} out of range (ofs={ofs} len={length} file={len(data)})")
    return Bsp(data, version, lumps)


def uncovered(bsp: Bsp) -> bytes:
    """Bytes of the file outside the header, the lumps and their padding."""
    covered = bytearray(len(bsp.data))
    covered[:HEADER_SIZE] = b"\x01" * HEADER_SIZE
    for ofs, length in bsp.lumps:
        end = min(len(bsp.data), ofs + ((length + 3) & ~3))
        covered[ofs:end] = b"\x01" * (end - ofs)
    return bytes(b for b, c in zip(bsp.data, covered) if not c)


def diff_records(name: str, fmt: str, fields: str, ref: bytes, cand: bytes) -> list[str]:
    size = struct.calcsize(fmt)
    out = []
    if len(ref) % size or len(cand) % size:
        out.append(f"  {name}: length not a multiple of record size {size} (ref={len(ref)} cand={len(cand)})")
    nref, ncand = len(ref) // size, len(cand) // size
    if nref != ncand:
        out.append(f"  {name}: record count differs (ref={nref} cand={ncand})")
    names = fields.split()
    shown = 0
    ndiff = 0
    for i in range(min(nref, ncand)):
        a = ref[i * size : (i + 1) * size]
        b = cand[i * size : (i + 1) * size]
        if a == b:
            continue
        ndiff += 1
        if shown < MAX_SHOWN:
            ra = struct.unpack(fmt, a)
            rb = struct.unpack(fmt, b)
            changed = ", ".join(f"{n}: {x!r} -> {y!r}" for n, x, y in zip(names, ra, rb) if x != y)
            out.append(f"  {name}[{i}]: {changed}")
            shown += 1
    if ndiff > shown:
        out.append(f"  {name}: ... {ndiff - shown} more differing records")
    if ndiff:
        out.insert(0, f"  {name}: {ndiff} of {min(nref, ncand)} records differ")
    return out


def diff_text(name: str, ref: bytes, cand: bytes) -> list[str]:
    a = ref.split(b"\n")
    b = cand.split(b"\n")
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            return [f"  {name}: first difference at line {i + 1}:", f"    ref : {x[:200]!r}", f"    cand: {y[:200]!r}"]
    return [f"  {name}: one is a prefix of the other (ref={len(ref)} bytes, cand={len(cand)} bytes)"]


def diff_blob(name: str, ref: bytes, cand: bytes) -> list[str]:
    out = []
    if len(ref) != len(cand):
        out.append(f"  {name}: length differs (ref={len(ref)} cand={len(cand)})")
    n = sum(1 for x, y in zip(ref, cand) if x != y)
    if n:
        first = next(i for i, (x, y) in enumerate(zip(ref, cand)) if x != y)
        out.append(f"  {name}: {n} bytes differ, first at offset {first} (ref=0x{ref[first]:02x} cand=0x{cand[first]:02x})")
    return out


def padding_ok(policy: str, ref: bytes, cand: bytes) -> bool:
    if policy == "ignore":
        return True
    if policy == "strict":
        return ref == cand
    zero = not any(cand)
    if policy == "zero":
        return zero
    return zero or ref == cand  # zero-or-equal


def compare(ref_path: str, cand_path: str, padding: str = "zero-or-equal") -> tuple[bool, list[str]]:
    """Return (identical_under_policy, report lines)."""
    ref = load(ref_path)
    cand = load(cand_path)
    report = []

    if ref.version != cand.version:
        report.append(f"  header: version ref={ref.version} cand={cand.version}")
    elif ref.version != BSPVERSION:
        report.append(f"  header: version {ref.version} (expected {BSPVERSION})")

    for i, (name, fmt, fields) in enumerate(LUMPS):
        a, b = ref.lump(i), cand.lump(i)
        if a != b:
            if fmt:
                report += diff_records(name, fmt, fields, a, b)
            elif name == "entities":
                report += diff_text(name, a, b)
            else:
                report += diff_blob(name, a, b)
        pa, pb = ref.padding(i), cand.padding(i)
        if not padding_ok(padding, pa, pb):
            report.append(f"  {name}: padding ref={pa.hex() or '-'} cand={pb.hex() or '-'} (policy {padding})")

    if ref.lumps != cand.lumps and not report:
        # Same contents but laid out differently: lump order changed.
        report.append(f"  header: lump layout differs: ref={ref.lumps} cand={cand.lumps}")
    elif ref.lumps != cand.lumps:
        report.append("  header: lump offsets/lengths differ (follows from the differences above)")

    if len(ref.data) != len(cand.data):
        report.append(f"  file: size ref={len(ref.data)} cand={len(cand.data)}")
    ua, ub = uncovered(ref), uncovered(cand)
    if ua != ub:
        report.append(f"  file: {len(ua)} vs {len(ub)} bytes outside any lump differ")

    return (not report, report)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ref")
    ap.add_argument("cand")
    ap.add_argument("--padding", choices=["zero-or-equal", "zero", "strict", "ignore"], default="zero-or-equal")
    ap.add_argument("-q", "--quiet", action="store_true", help="exit status only")
    args = ap.parse_args()

    try:
        same, report = compare(args.ref, args.cand, args.padding)
    except BspError as e:
        print(f"bspdiff: {e}", file=sys.stderr)
        return 2
    if not args.quiet:
        if same:
            print(f"identical (padding policy: {args.padding})")
        else:
            print(f"{args.ref} vs {args.cand}:")
            print("\n".join(report))
    return 0 if same else 1


if __name__ == "__main__":
    sys.exit(main())
