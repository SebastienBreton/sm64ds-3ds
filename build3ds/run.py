#!/usr/bin/env python3
"""Run one 3DS test; require program PASS and fresh, clean emulator diagnostics."""
import argparse
import hashlib
import json
import fcntl
import os
from pathlib import Path
import re
import signal
import subprocess
import time
from run_result import assess_run, movie_playback_failure
from azahar_movie import parse as parse_movie
from azahar_config import model_override, model_failure
import runner_journal

ap = argparse.ArgumentParser()
ap.add_argument("binary", type=Path)
ap.add_argument("--timeout", type=float, default=180)
ap.add_argument("--env", action="append", default=[])
ap.add_argument("--output", type=Path)
ap.add_argument("--3ds-model", dest='model', choices=['old', 'new'], help="Temporarily select a 3DS model; require guest verification and restore the original config")
movie = ap.add_mutually_exclusive_group()
movie.add_argument("--movie-play", type=Path, help="Replay an Azahar CTM input recording")
movie.add_argument("--movie-record", type=Path, help="Record emulator HID input to a new CTM file")
a = ap.parse_args()
if a.model:
    required_model = 'SM64DS_REQUIRE_3DS_MODEL='
    if any(item.startswith(required_model) and item != required_model+a.model for item in a.env):
        ap.error('Conflicting guest model requirement')
    if not any(item.startswith(required_model) for item in a.env):
        a.env.append(required_model+a.model)
movie_args = []
movie_metadata = None
if a.movie_play:
    a.movie_play = a.movie_play.resolve(strict=True)
    movie_metadata = {"mode": "play", "path": str(a.movie_play),
                      **parse_movie(a.movie_play.read_bytes())}
    movie_args = ["-p", str(a.movie_play)]
elif a.movie_record:
    a.movie_record = a.movie_record.resolve()
    if a.movie_record.exists():
        ap.error("movie recording already exists; choose a new path")
    a.movie_record.parent.mkdir(parents=True, exist_ok=True)
    movie_metadata = {"mode": "record", "path": str(a.movie_record)}
    # The 2126.1.2 meta launcher probes arguments with short getopt and can
    # misclassify letters in long --movie-* options as compression commands.
    movie_args = ["-r", str(a.movie_record), "-a", "SM64DS validation"]
binary = a.binary.resolve(strict=True)
elf = binary.with_suffix(".elf")
if binary.suffix == ".3dsx" and elf.exists() and elf.stat().st_mtime_ns > binary.stat().st_mtime_ns:
    ap.error(f"stale 3DSX: {elf.name} is newer; rebuild the corresponding _3dsx target")
binary_sha256 = hashlib.sha256(binary.read_bytes()).hexdigest()
app = Path(os.environ.get("AZAHAR_APP", str(Path.home() / "Downloads/azahar-macos-universal-2126.1.2/Azahar.app")))
sd = Path.home() / "Library/Application Support/Azahar/sdmc/sm64ds"
sd.mkdir(parents=True, exist_ok=True)
lock = (sd / ".test.lock").open("w")
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
def pids():
    r = subprocess.run(["pgrep", "-f", "Azahar.app/Contents/MacOS/azahar"], capture_output=True, text=True)
    return set(map(int, r.stdout.split()))
# The user requested that Azahar be killed between every run. The SD lock
# prevents two test launchers from terminating each other's emulator.
def stop_emulators(owned):
    for pid in owned:
        try:
            os.kill(pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
    end = time.monotonic() + 2
    while owned & pids() and time.monotonic() < end:
        time.sleep(.05)
    for pid in owned & pids():
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
    end = time.monotonic() + 2
    while owned & pids() and time.monotonic() < end:
        time.sleep(.05)
    if owned & pids():
        raise RuntimeError("Azahar did not terminate")

stop_emulators(pids())
paths = [sd / "env.txt", sd / "autoexit"]
config = Path.home() / "Library/Application Support/Azahar/config/qt-config.ini"
journal = sd / ".runner-restoration.json"
if runner_journal.recover(journal, [*paths, config]):
    print("Recovered original emulator settings from an interrupted run", flush=True)
if a.model:
    paths.append(config)
saved = {p: p.read_bytes() if p.exists() else None for p in paths}
runner_journal.save(journal, saved)
def interrupted(signum, frame):
    raise KeyboardInterrupt
signal.signal(signal.SIGTERM, interrupted)
log = sd / "log.txt"
result = "TIMEOUT"
early_failure_reason = None
emulator_log = Path.home() / "Library/Application Support/Azahar/log/azahar_log.txt"
capture_started = time.time()
try:
    if a.model:
        if saved[config] is None:
            raise ValueError("Azahar config missing; cannot safely override model")
        config.write_bytes(model_override(saved[config], a.model))
    if a.env:
        for item in a.env:
            if "=" not in item or "\n" in item:
                raise ValueError("Expected KEY=VALUE")
        paths[0].write_text("\n".join(a.env) + "\n")
    paths[1].touch()
    log.unlink(missing_ok=True)
    subprocess.run(["open", "-n", "-g", "-a", str(app), "--args", *movie_args, str(binary)], check=True)
    startup = time.monotonic() + 5
    while not pids() and time.monotonic() < startup:
        time.sleep(.05)
    deadline = time.monotonic() + a.timeout
    diagnostic_offset = 0
    diagnostic_tail = ""
    while time.monotonic() < deadline:
        if not log.exists() and emulator_log.exists() and emulator_log.stat().st_mtime >= capture_started:
            with emulator_log.open(errors="replace") as loader_log:
                loader_diagnostic = loader_log.read(65536)
            if re.search(r'Failed to load ROM \(Error \d+\)', loader_diagnostic):
                result = "FAIL"
                early_failure_reason = "emulator_load_failure"
                break
        if a.movie_play and emulator_log.exists() and emulator_log.stat().st_mtime >= capture_started:
            # Service ordering changes between native builds/configurations.
            # A mismatched CTM can still inject sporadic touches and produce
            # a program PASS. Stop immediately rather than interpreting it.
            with emulator_log.open(errors="replace") as diagnostics:
                if emulator_log.stat().st_size < diagnostic_offset:
                    diagnostic_offset = 0
                diagnostics.seek(diagnostic_offset)
                diagnostic_chunk = diagnostic_tail + diagnostics.read()
                diagnostic_offset = diagnostics.tell()
            diagnostic_tail = diagnostic_chunk[-512:]
            if movie_playback_failure(diagnostic_chunk) == "movie_playback_invalid":
                result = "FAIL"
                early_failure_reason = "movie_playback_invalid"
                break
        content = log.read_text(errors="replace") if log.exists() else ""
        matches = re.findall(r"^(PASS|FAIL)$", content, re.M)
        if re.search(r"^FATAL:", content, re.M):
            result = "FAIL"
            break
        if matches:
            result = matches[-1]
            break
        time.sleep(.25)
except KeyboardInterrupt:
    result = "FAIL"
    early_failure_reason = "interrupted"
finally:
    try:
        stop_emulators(pids())
    finally:
        for path, data in saved.items():
            if data is None:
                path.unlink(missing_ok=True)
            else:
                path.write_bytes(data)
        journal.unlink(missing_ok=True)
content = log.read_text(errors="replace") if log.exists() else "(no log)"
# Apply the same verdict even without --output. Do not reuse a previous
# emulator's diagnostics as evidence for this execution.
emulator_fresh = emulator_log.exists() and emulator_log.stat().st_mtime >= capture_started
emulator_text = emulator_log.read_text(errors="replace") if emulator_fresh else ""
application_result = result
result, failure_reason, unmapped = assess_run(application_result, emulator_text, emulator_fresh)
requested_model = a.model
if requested_model:
    model_verification_failure = model_failure(content, requested_model)
    if model_verification_failure and result == "PASS":
        result, failure_reason = "FAIL", model_verification_failure
if early_failure_reason:
    failure_reason = early_failure_reason
if movie_metadata:
    if a.movie_record:
        if a.movie_record.exists():
            movie_metadata["sha256"] = hashlib.sha256(a.movie_record.read_bytes()).hexdigest()
        else:
            result, failure_reason = "FAIL", "movie_recording_not_saved"
    # A running game can still PASS after the emulator desynchronizes a movie.
    if a.movie_play:
        movie_failure = movie_playback_failure(emulator_text)
        movie_metadata["playback_failure"] = movie_failure
        if movie_failure and result == "PASS":
            result, failure_reason = "FAIL", movie_failure
if a.output:
    a.output.mkdir(parents=True, exist_ok=True)
    (a.output / "log.txt").write_text(content)
    # Preserve the emulator diagnostic before another run replaces it. A
    # program-level PASS does not establish absence of invalid memory access.
    (a.output / "azahar-log.txt").write_text(emulator_text)
    (a.output / "run.json").write_text(json.dumps({
        "binary": str(binary), "sha256": binary_sha256,
        "environment_overrides": a.env, "result": result,
        "requested_3ds_model": requested_model,
        "model_verification_failure": model_failure(content, requested_model) if requested_model else None,
        "application_result": application_result, "failure_reason": failure_reason,
        "emulator_log_fresh": emulator_fresh,
        "temporary_settings_restored": all(
            (path.read_bytes() if path.exists() else None) == data
            for path, data in saved.items()),
        "original_config_sha256": hashlib.sha256(saved[config]).hexdigest()
            if config in saved and saved[config] is not None else None,
        "restored_config_sha256": hashlib.sha256(config.read_bytes()).hexdigest()
            if config in saved and config.exists() else None,
        "started_unix": capture_started,
        "movie": movie_metadata,
        "unmapped_access_count": unmapped,
    }, indent=2) + "\n")
    # Only retain captures written during this run.
    import shutil
    for path in [*sd.glob("*.bmp"), *sd.glob("stage-frame-*-camera.json")]:
        if path.stat().st_mtime >= capture_started:
            shutil.copy2(path, a.output / path.name)
print(re.sub(r"\x1b\[[0-9;]*[A-Za-z]", "", content))
print("RESULT:", result)
if failure_reason:
    print("REASON:", failure_reason, "unmapped accesses:", unmapped)
raise SystemExit(0 if result == "PASS" else 1)
