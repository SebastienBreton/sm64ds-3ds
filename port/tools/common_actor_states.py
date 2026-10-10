#!/usr/bin/env python3
"""Reviewed common ov2 PMF batch; preserve original initializer order and bodies."""
import argparse, re
from pathlib import Path
from actor_pmfs import generate

# The 24 compiled common profiles add coin and Yoshi egg PMF tables to the
# previously installed player, 1-up and signpost state initializers.
BATCHES = [
    ('Coin', 'src/__sinit_ov002_02100c50.c', '__sinit_ov002_02100c50', 'data_ov002_0210dc70', 9),
    ('YoshiEgg', 'src/__sinit_ov002_02107118.c', '__sinit_ov002_02107118', 'data_ov002_02110a5c', 8),
]

def generate_batch(root):
    parts=[]
    checks=[]
    for kind, source, constructor, destination, count in BATCHES:
        text=generate(root, 2, 0x020ad660, source)
        records=re.findall(r'State (data_ov002_[0-9a-f]+)=&NativeActorStates::state(\d+);', text)
        if len(records)!=count: raise ValueError('common PMF count changed: '+kind)
        text=text.replace('NativeActorStates', 'Native'+kind+'States')
        text=re.sub(r'\bState\b', 'Native'+kind+'State', text)
        parts.append(text)
        checks.append('extern "C" unsigned char '+destination+'[];')
        body=[]
        # Validate original destination order, which differs from ROM address order.
        original=(root/source).read_text()
        order=re.findall(r'(?:dst|'+destination+r')\[(\d+)\]\s*=\s*(data_ov002_[0-9a-f]+)', original)
        if [int(i) for i,_ in order]!=list(range(count)): raise ValueError('common PMF copy layout changed: '+kind)
        for index, symbol in order:
            body.append(f' if(std::memcmp({destination}+{int(index)*8}, &{symbol}, 8))return 0;')
        checks.append('extern "C" unsigned sm64ds_check_'+kind.lower()+'_states(){\n'+'\n'.join(body)+f'\n return {count};\n}}')
    # Full bank79 now owns its Whomp relocation table and complete BSS.
    # Coin/Egg PMF generation above retains the original helper references.
    parts.append('#include <cstring>\n'+'\n'.join(checks))
    return '\n'.join(parts)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('root',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();a.output.write_text(generate_batch(a.root))
