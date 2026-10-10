"""Catalog the files inside SM64DS's NARC archives for the port's fs seam.

The game addresses archive members with file IDs >= 0x8000: archive i owns
[first, end) from the 13-entry table at 0x0208ecf4 in the ARM9 image
(func_020186c0 walks it; first = 0x8000 + i*0x400), and a member's ID is
first + its index in the NARC. This reads that table from the local
extracted/arm9_dec.bin, unpacks each archive under extracted/dsd/archives/,
and writes build/assets/archives.tsv (id, hex_id, archive, path) -- local-only,
like tools/asset_catalog.py. Requires ndspy.

    python port/tools/archive_catalog.py
"""
import os
import pathlib
import struct
import sys

import ndspy.narc

REPO = pathlib.Path(os.environ.get('SM64DS_DECOMP_DIR') or pathlib.Path(__file__).resolve().parents[2] / 'sm64ds-decomp')
ARM9 = REPO / "extracted" / "arm9_dec.bin"
FILES = REPO / "extracted" / "dsd" / "files"
OUT_DIR = REPO / "extracted" / "dsd" / "archives"
TSV = REPO / "build" / "assets" / "archives.tsv"

ARM9_BASE = 0x02004000
TABLE = 0x0208ECF4
ENTRIES = 13
ENTRY_SIZE = 0x14   # f0 loaded ptr, f4, u16 first, u16 end, char *short_name, char *path


def cstr(image: bytes, addr: int) -> str:
    off = addr - ARM9_BASE
    return image[off:image.index(b"\0", off)].decode("ascii")


def main() -> None:
    if not ARM9.is_file():
        sys.exit(f"missing {ARM9} -- run tools/unpack.py on your ROM first")
    image = ARM9.read_bytes()
    rows = []
    for i in range(ENTRIES):
        _, _, first, end, name_p, path_p = struct.unpack_from(
            "<IIHHII", image, TABLE - ARM9_BASE + i * ENTRY_SIZE)
        name, narc_path = cstr(image, name_p), cstr(image, path_p)
        narc = ndspy.narc.NARC((FILES / narc_path.lstrip("/")).read_bytes())
        if len(narc.files) != end - first:
            sys.exit(f"{narc_path}: {len(narc.files)} members, table says {end - first}")
        for k, data in enumerate(narc.files):
            member = narc.filenames.filenameOf(k)
            rel = pathlib.Path("archives") / name / member
            dst = REPO / "extracted" / "dsd" / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            dst.write_bytes(data)
            rows.append((first + k, name, rel.as_posix()))
    TSV.parent.mkdir(parents=True, exist_ok=True)
    with TSV.open("w", encoding="ascii") as f:
        f.write("id\thex_id\tarchive\tpath\n")
        for fid, name, rel in rows:
            f.write(f"{fid}\t0x{fid:04x}\t{name}\t{rel}\n")
    print(f"{len(rows)} archive members from {ENTRIES} archives -> {TSV}")


if __name__ == "__main__":
    main()
