#!/usr/bin/env python3
"""Compare direct emulator captures exactly; run with the workspace .venv Python."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("reference", type=Path)
parser.add_argument("candidate", type=Path)
parser.add_argument("--report", type=Path)
args = parser.parse_args()
files = sorted(args.reference.glob("*.bmp"))
results = []
for original in files:
    candidate = args.candidate / original.name
    result = {"file": original.name, "equal": False}
    if not candidate.is_file():
        result["error"] = "missing candidate"
    else:
        with Image.open(original) as a, Image.open(candidate) as b:
            result["reference_size"] = a.size
            result["candidate_size"] = b.size
            if a.size == b.size:
                difference = ImageChops.difference(a.convert("RGB"), b.convert("RGB"))
                result["difference_bounds"] = difference.getbbox()
                result["equal"] = result["difference_bounds"] is None
            else:
                result["error"] = "different dimensions"
    results.append(result)
report = {"reference": str(args.reference.resolve()),
          "candidate": str(args.candidate.resolve()),
          "pass": bool(results) and all(r["equal"] for r in results),
          "captures": results}
text = json.dumps(report, indent=2)
print(text)
if args.report:
    args.report.write_text(text + "\n")
raise SystemExit(0 if report["pass"] else 1)
