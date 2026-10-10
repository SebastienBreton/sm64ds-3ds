"""Small, lossless edits for temporary Azahar model validation runs."""
import re


def model_override(data, model):
    if model not in ('old', 'new'):
        raise ValueError('Expected old or new 3DS model')
    lines = data.decode('utf-8').splitlines(keepends=True)
    section = None
    seen = {}
    wanted = {'is_new_3ds': 'true' if model == 'new' else 'false',
              'is_new_3ds\\default': 'false'}
    for index, line in enumerate(lines):
        if line.strip().startswith('['):
            section = line.strip()
        if section != '[System]' or '=' not in line:
            continue
        key = line.split('=', 1)[0]
        if key not in wanted:
            continue
        if key in seen:
            raise ValueError(f'Duplicate System setting: {key}')
        seen[key] = index
    if set(seen) != set(wanted):
        raise ValueError('Azahar System model settings missing')
    for key, index in seen.items():
        ending = '\r\n' if lines[index].endswith('\r\n') else '\n' if lines[index].endswith('\n') else ''
        lines[index] = key + '=' + wanted[key] + ending
    return ''.join(lines).encode('utf-8')


def model_failure(log, requested):
    matches = re.findall(r'^CPU mode: model_query=([0-9a-fA-F]+) new3ds=([01]) speedup_requested=[01]$', log, re.M)
    if not matches:
        return 'model_not_verified_by_guest'
    expected = requested == 'new'
    if any(int(result, 16) & 0x80000000 or bool(int(model)) != expected
           for result, model in matches):
        return 'model_mismatch'
    return None
