"""Dump the ROM's NitroFS files to extracted/dsd/files/<path> (the layout the
port's hal/fs.cpp reads), using ndspy. Local-only, like tools/unpack.py."""
import pathlib, sys
import ndspy.rom
rom = ndspy.rom.NintendoDSRom.fromFile(sys.argv[1])
out = pathlib.Path(sys.argv[2])
n = 0
for fid, data in enumerate(rom.files):
    path = rom.filenames.filenameOf(fid)
    if path is None:
        continue
    dst = out / path
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)
    n += 1
print(f"wrote {n} files to {out}")
