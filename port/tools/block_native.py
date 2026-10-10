from pathlib import Path
import sys
s=Path(sys.argv[1]).read_text()
for old,new in [('int daObjBlockL_c::OnAttacked1(dActor_c &other)\n{\n    int b = (actorID == 0x11);\n    if (b) return;\n    Kill();\n}', 'int daObjBlockL_c::OnAttacked1(dActor_c &other)\n{\n    // ROM callback preserves this on no-op; otherwise forwards slot31 r0.\n    if (actorID == 0x11) return (int)(unsigned long)this;\n    typedef int (*KillRegisterCall)(void*);\n    return (*(KillRegisterCall**)this)[31](this);\n}'), ('int daObjBlockL_c::OnAttacked2(dActor_c &other)\n{\n    int b = (actorID == 0x11);\n    if (b) {\n        if (other.param1 != 2) return;\n        Kill();\n        return;\n    }\n    Kill();\n}', 'int daObjBlockL_c::OnAttacked2(dActor_c &other)\n{\n    // ROM callback preserves this on no-op; otherwise forwards slot31 r0.\n    if (actorID == 0x11 && other.param1 != 2) return (int)(unsigned long)this;\n    typedef int (*KillRegisterCall)(void*);\n    return (*(KillRegisterCall**)this)[31](this);\n}')]:
    assert s.count(old)==1, "review block callback ABI"
    s=s.replace(old,new)
Path(sys.argv[2]).write_text(s)
