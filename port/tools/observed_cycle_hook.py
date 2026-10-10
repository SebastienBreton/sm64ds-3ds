"""Add the optional 3DS pacing observer at the original outer-loop boundary.

Runs after hostgen, only for func_020197b8. It never changes decomp src/.
"""
import argparse
from pathlib import Path

DECLARATION='void sm64ds_observed_cycle_begin(void) __attribute__((weak));\n'
CALL='        if(sm64ds_observed_cycle_begin)sm64ds_observed_cycle_begin();\n'

def add_hook(source):
    if 'sm64ds_observed_cycle_begin' in source:
        raise ValueError('cycle hook already present; regenerate with hostgen first')
    signature='void func_020197b8(void) {\n'
    boundary='    do {\n        unsigned short v;\n        data_0209d50c = 0xb; func_02019ac4();'
    if source.count(signature)!=1 or source.count(boundary)!=1:
        raise ValueError('original outer-loop boundary changed; review before inserting hook')
    if 'extern "C" {' not in source:
        raise ValueError('expected C-linkage hostgen output')
    source=source.replace(signature,DECLARATION+'\n'+signature,1)
    return source.replace(boundary,boundary.replace('    do {\n','    do {\n'+CALL,1),1)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path)
    args=parser.parse_args()
    text=args.source.read_text()
    args.source.write_text(add_hook(text))

if __name__=='__main__':main()
