#!/usr/bin/env python3
"""Compile native local input transfer with the ROM's overlapping arrays explicit."""
from pathlib import Path
import argparse

def generate(source):
    aliases={"data_020a117c":0x28,"data_020a11a0":0x4c,"data_020a11c4":0x70}
    for name,offset in aliases.items():
        declaration=f"extern u16 {name}[4];"
        if source.count(declaration)!=1:raise ValueError("Changed native declaration: "+name)
        source=source.replace(declaration,f"#define {name} ((u16*)(data_020a1154 + 0x{offset:x}))")
    return "// Generated native local transfer. Explicit ROM aliases prevent false no-alias reordering.\n"+source

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    a.output.write_text(generate(a.source.read_text()))
