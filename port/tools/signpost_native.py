#!/usr/bin/env python3
"""Keep signpost attack callback's ARM return register defined under GCC."""
from pathlib import Path
import sys

source=Path(sys.argv[1]).read_text()
old='''int daObjTatefuda_c::OnAttacked1(dActor_c &other)
{
    int isCode = (other.actorID == ACTOR_BOMBHEI);
    if (isCode) {
        Kill();
    }
}'''
new='''int daObjTatefuda_c::OnAttacked1(dActor_c &other)
{
    // ROM 020bb23c leaves r0=this on the no-op branch, otherwise forwards
    // the register left by virtual Kill (slot31). Falling off an int C++
    // function is undefined under GCC, even when its caller ignores r0.
    if (other.actorID == ACTOR_BOMBHEI) {
        typedef int (*KillRegisterCall)(void*);
        return (*(KillRegisterCall**)this)[31](this);
    }
    return (int)(unsigned long)this;
}'''
assert source.count(old)==1,'signpost callback changed; review native ABI adapter'
Path(sys.argv[2]).write_text('// Generated from original signpost source.\n'+source.replace(old,new))
