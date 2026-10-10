#!/usr/bin/env python3
"""Validate archived explicit-flush binaries, then compare the same loop binary.

Every case uses run.py's exclusive emulator lock and kill-between-runs policy.
Run only after reserving Azahar with other agents.
"""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent.parent
binary_dir = root / "build3ds/validation/gpu-flush-binaries"
common = ["SM64DS_NEW3DS_SPEEDUP=1", "PLAYER_FULL_GPU_FLUSH=0"]
loop = ["ORIGINAL_LOOP_FRAMES=60", "ORIGINAL_LOOP_GPU=1", "ORIGINAL_LOOP_NDSP=1",
        "PLAYER_NATIVE_COMPOSITE=1", "PLAYER_PREPARED_PPU=1", "PLAYER_GPU_BLIT=1",
        "PLAYER_GPU_SHADOWS=1", "PLAYER_PROFILE_GPU=1", "PLAYER_PROFILE_PPU=1",
        "SM64DS_FORCE_3D=0", "SM64DS_NEW3DS_SPEEDUP=1"]
cases = [
    ("flush-native-composite", "smoke_native_composite", common+["PLAYER_GPU_BLIT=1"]),
    ("flush-texture-reuse", "smoke_gpu_texture_reuse", common),
    ("flush-shadow-stereo", "smoke_gpu_shadow", common+["SM64DS_FORCE_3D=1"]),
    ("flush-loop-full", "smoke_original_loop", loop+["PLAYER_FULL_GPU_FLUSH=1"]),
    ("flush-loop-explicit", "smoke_original_loop", loop+["PLAYER_FULL_GPU_FLUSH=0"]),
]
for name, binary, environment in cases:
    output = root / "build3ds/validation" / name
    args = [sys.executable, str(root/"build3ds/run.py"), str(binary_dir/(binary+".3dsx")),
            "--timeout", "120", "--output", str(output)]
    for setting in environment:
        args += ["--env", setting]
    print("Running", name, flush=True)
    result = subprocess.run(args, capture_output=True, text=True)
    (root/"build3ds"/(name+"-runner.log")).write_text(result.stdout+result.stderr)
    if result.returncode:
        print(result.stdout[-3000:]+result.stderr[-3000:])
        raise SystemExit(result.returncode)
    print("PASS", name, flush=True)
