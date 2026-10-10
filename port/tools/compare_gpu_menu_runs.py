#!/usr/bin/env python3
"""Compare archived deterministic menu runs; reject missing/mismatched pixels."""
import argparse
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("runs", nargs="+", type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
images = ("player-native-geometry.bmp", "player-native-upper.bmp",
          "player-native-bottom.bmp", "player-gpu-left.bmp", "player-gpu-right.bmp")
records = []
for directory in args.runs:
    verdict = json.loads((directory / "run.json").read_text())
    assert verdict["result"] == "PASS", (directory, verdict)
    assert verdict["unmapped_access_count"] == 0, directory
    assert verdict["requested_3ds_model"] == "old", directory
    assert verdict["model_verification_failure"] is None, directory
    log = (directory / "log.txt").read_text()
    assert "new3ds=0" in log, directory
    assert "frame=959 state=0 substate=0" in log, directory
    match = re.search(r"Menu FPS measured120 ms/frame: game=([\d.]+) audio=([\d.]+) presentation=([\d.]+) total=([\d.]+) fps=([\d.]+)", log)
    assert match, directory
    timings = dict(zip(("game_ms", "audio_ms", "presentation_ms", "total_ms", "updates_per_second"), map(float, match.groups())))
    records.append({"directory": str(directory.resolve()), "binary_sha256": verdict["sha256"],
                    "environment": verdict["environment_overrides"], "timings": timings,
                    "images": {name: hashlib.sha256((directory / name).read_bytes()).hexdigest() for name in images}})
for record in records[1:]:
    assert record["binary_sha256"] == records[0]["binary_sha256"], "A/B binary differs"
    assert record["images"] == records[0]["images"], ("pixel captures differ", record["directory"])
args.output.mkdir(parents=True, exist_ok=True)
(args.output / "report.json").write_text(json.dumps({"scope": "One original menu Behavior/Render, one callback/VBlank commit, 1092 audio samples and one presentation per update. Azahar CPU100 classic model; not a physical hardware FPS measurement.", "all_five_images_identical": True, "runs": records}, indent=2) + "\n")
for record in records:
    print(Path(record["directory"]).name, record["timings"])
print("PASS: same binary, verified classic model, zero Unmapped, five captures byte-identical")
