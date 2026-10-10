#!/usr/bin/env python3
"""Run a buttons-only telemetry route in the unchanged retail DS emulator.

Loads one declared starting savestate, dismisses dialogue with A press/release,
and records the same integer telemetry schema as native gameplay probes.
"""
import argparse
import hashlib
import json
import subprocess
import shutil
from pathlib import Path
from player_telemetry import inspect_ram

ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument("--runner",type=Path,required=True)
ap.add_argument("--rom",type=Path,required=True)
ap.add_argument("--state",type=Path,required=True)
ap.add_argument("--route",type=Path,required=True)
ap.add_argument("--output",type=Path,required=True)
ap.add_argument("--geometry",action="store_true",help="Capture retail polygon stream, video registers and GX matrices at each checkpoint")
ap.add_argument("--pixel-buffers",action="store_true",help="Read final DS software renderer attribute/depth buffers; requires the diagnostic runner and --geometry")
ap.add_argument("--pose-observe",action="store_true",help="Observe original CPU entry/return and body geometry commands during each checkpoint update; requires pose diagnostic runner")
args=ap.parse_args()
if args.pixel_buffers and not args.geometry:ap.error("--pixel-buffers requires --geometry")
out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
route=json.loads(args.route.read_text())
commands=[];updates=0;route_origin=0;failure=None
proc=subprocess.Popen([str(args.runner.resolve()),str(args.rom.resolve())],cwd=out,
                      stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=open(out/"emulator-stderr.log","w"),text=True)
def send(command,until=None):
    commands.append(command);proc.stdin.write(command+"\n");proc.stdin.flush()
    if until:
        while True:
            line=proc.stdout.readline()
            if not line: raise RuntimeError("DS reference exited before acknowledgement")
            if line.startswith(until): return

def run(count,keys=0,observe_label=None):
    global updates
    if observe_label and args.pose_observe:
        if count>1:run(count-1,keys)
        send(f"poseobserve pose-{observe_label}")
        run(1,keys)
        send("poseobserve off")
        return
    send(f"run {count*2} {0xfff^keys:03x}","ran ");updates+=count

def snapshot(label,keys=0,screenshot=False):
    prefix=out/label
    send(f"ram {label}.ram")
    if args.geometry:
        send(f"save {label}.state")
        send(f"polygons {label}.polygons")
        if args.pixel_buffers:send(f"pixelbuffers {label}")
        send(f"video {label}")
        send(f"registers {label}.registers")
        send("matrices")
        matrix_lines=[proc.stdout.readline().strip(),proc.stdout.readline().strip()]
        (out/(label+".matrices")).write_text("\n".join(matrix_lines)+"\n")
    # shot also acts as a command barrier after the RAM write.
    send(f"shot {label}.ppm","shot ")
    sample=inspect_ram(str(prefix)+".ram",label,updates-route_origin,keys)
    sample["emulator_updates_since_load"]=updates
    (out/(label+".json")).write_text(json.dumps(sample,indent=2)+"\n")
    if not screenshot:(out/(label+".ppm")).unlink()
    return sample

samples=[]
try:
    shutil.copy2(args.state,out/"before.state")
    send("load before.state")
    before=snapshot("before-dialogue",screenshot=True)
    if before["level"]!=route["level"] or not (before["persistent_flags"]&0x40):
        raise RuntimeError("Expected legitimate castle interior state with earned key")
    for attempt in range(30):
        if not before["message"] and not before["script_active"]: break
        run(1,1);run(44)
        before=snapshot(f"dialogue-{attempt:02}",screenshot=True)
    else:raise RuntimeError("Dialogue did not dismiss through buttons")
    route_origin=updates
    run(route.get("settle_updates",60),observe_label="start")
    samples.append(snapshot("start",screenshot=True))
    for segment in route["segments"]:
        run(segment["updates"],segment["keys"],segment["checkpoint"])
        sample=snapshot(segment["checkpoint"],segment["keys"],True)
        samples.append(sample)
        if sample["level"]!=route["level"]:raise RuntimeError("Route left its declared level")
    send("save after.state")
    # Finish observation callbacks already entered by the final route update.
    # Preserve after.state at the declared route endpoint, before this idle tail.
    if args.pose_observe:send("run 1 fff","ran ")
except Exception as exc:
    failure=str(exc)
finally:
    if proc.poll() is None:
        send("quit");proc.stdin.close();proc.wait(timeout=30)
    (out/"commands.txt").write_text("\n".join(commands)+"\n")
result={"schema":"sm64ds-player-route-v1","ram_writes":False,
        "starting_state":str(args.state.resolve()),"starting_state_sha256":hashlib.sha256(args.state.read_bytes()).hexdigest(),
        "geometry":args.geometry,"pixel_buffers":args.pixel_buffers,"pose_observe":args.pose_observe,"pose_observer_tail_ds_frames":int(args.pose_observe),"runner_sha256":hashlib.sha256(args.runner.read_bytes()).hexdigest(),"rom_sha256":hashlib.sha256(args.rom.read_bytes()).hexdigest(),"route":route,"samples":samples,"emulator_exit":proc.returncode,"completed":failure is None,"failure":failure}
(out/"telemetry.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps({"samples":len(samples),"updates":updates,"exit":proc.returncode,"output":str(out)}))

if failure: raise SystemExit(failure)
