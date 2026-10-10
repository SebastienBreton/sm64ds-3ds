"""Durable restoration of temporary emulator settings after an interrupted run."""
import base64
import json
import os
from pathlib import Path


def save(path, originals):
    record = {str(p.resolve()): None if data is None else base64.b64encode(data).decode('ascii')
              for p, data in originals.items()}
    temporary = path.with_suffix('.tmp')
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(descriptor, 'w') as stream:
        json.dump(record, stream)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def recover(path, allowed_paths):
    if not path.exists():
        return False
    allowed = {str(p.resolve()) for p in allowed_paths}
    record = json.loads(path.read_text())
    if not isinstance(record, dict) or not set(record) <= allowed:
        raise ValueError('Unexpected path in emulator restoration journal')
    # Validate every entry before restoring any file.
    decoded = {Path(p): None if data is None else base64.b64decode(data, validate=True)
               for p, data in record.items()}
    for target, data in decoded.items():
        if data is None:
            target.unlink(missing_ok=True)
        else:
            target.write_bytes(data)
    path.unlink()
    return True
