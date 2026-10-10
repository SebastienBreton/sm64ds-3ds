#!/usr/bin/env python3
"""Inspect/edit Azahar 2126.1.2 CTM inputs, retaining recorded service ordering.

Sample numbers are zero-based HID pad samples, NOT rendered/game frames.
The baseline must come from the same emulator/build/service configuration.
Other sensors and the initial clock/timing header remain byte-identical.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

HEADER = 256
RECORD = 7


def parse(blob):
    if len(blob) < HEADER or blob[:4] != b'CTM\x1b':
        raise ValueError('invalid CTM header')
    if (len(blob) - HEADER) % RECORD:
        raise ValueError('truncated controller record')
    types = Counter(blob[HEADER::RECORD])
    if set(types) - set(range(6)):
        raise ValueError('unknown controller record type')
    declared = struct.unpack_from('<Q', blob, 84)[0]
    if not declared or declared != types[0] or types[1] != types[0]:
        raise ValueError('pad/touch counts do not match the header')
    return {'samples': declared, 'record_types': dict(types),
            'revision': blob[12:32].hex(),
            'sha256': hashlib.sha256(blob).hexdigest()}


def bounded(value, lower, upper, name):
    if type(value) is not int or not lower <= value <= upper:
        raise ValueError(f'{name} must be an integer in [{lower}, {upper}]')
    return value


def edit(blob, events):
    info = parse(blob)
    if not isinstance(events, list):
        raise ValueError('events must be a list')
    normalized = []
    for event in events:
        if not isinstance(event, dict):
            raise ValueError('each event must be an object')
        event = dict(event)
        if 'touch_ds' in event:
            if 'touch' in event:
                raise ValueError('choose either touch (3DS) or touch_ds coordinates')
            touch = event.pop('touch_ds')
            if touch is not None:
                if len(touch) != 2:
                    raise ValueError('touch_ds needs x,y or null')
                x = bounded(touch[0], 0, 255, 'DS touch x')
                y = bounded(touch[1], 0, 191, 'DS touch y')
                # Inverse of the port's floor(px*256/320), floor(py*192/240).
                # Round up so every original DS pixel survives the conversion.
                touch = [(x * 5 + 3) // 4, (y * 5 + 3) // 4]
            event['touch'] = touch
        normalized.append(event)
    events = normalized
    previous_end = 0
    for event in events:
        if not isinstance(event, dict) or set(event) - {'start', 'end', 'buttons', 'circle', 'touch'}:
            raise ValueError('unknown event field')
        start = bounded(event['start'], 0, info['samples'] - 1, 'start')
        end = bounded(event['end'], start + 1, info['samples'], 'end')
        if start < previous_end:
            raise ValueError('events must be sorted and non-overlapping')
        previous_end = end
        if 'buttons' in event:
            bounded(event['buttons'], 0, 0x3fff, 'buttons')
        if 'circle' in event:
            if len(event['circle']) != 2:
                raise ValueError('circle needs x,y')
            for value in event['circle']:
                bounded(value, -156, 156, 'circle axis')
        if 'touch' in event and event['touch'] is not None:
            if len(event['touch']) != 2:
                raise ValueError('touch needs x,y or null')
            bounded(event['touch'][0], 0, 319, 'touch x')
            bounded(event['touch'][1], 0, 239, 'touch y')
    result = bytearray(blob)
    sample, event_index = -1, 0
    pending_touch = False
    for offset in range(HEADER, len(blob), RECORD):
        kind = blob[offset]
        if kind == 0:
            if pending_touch:
                raise ValueError('pad sample missing its touch record')
            sample += 1
            pending_touch = True
        elif kind == 1:
            if not pending_touch:
                raise ValueError('touch record before pad sample')
            pending_touch = False
        while event_index < len(events) and sample >= events[event_index]['end']:
            event_index += 1
        if event_index == len(events) or sample < events[event_index]['start']:
            continue
        event = events[event_index]
        if kind == 0:
            if 'buttons' in event:
                struct.pack_into('<H', result, offset + 1, event['buttons'])
            if 'circle' in event:
                struct.pack_into('<hh', result, offset + 3, *event['circle'])
        elif kind == 1 and 'touch' in event:
            touch = event['touch']
            struct.pack_into('<HHB', result, offset + 1, *(touch or (0, 0)), int(touch is not None))
    if pending_touch:
        raise ValueError('last pad sample missing touch record')
    parse(result)
    return bytes(result)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input', type=Path)
    ap.add_argument('--events', type=Path, help='JSON list of start/end sample intervals')
    ap.add_argument('--output', type=Path)
    args = ap.parse_args()
    original = args.input.read_bytes()
    source = parse(original)
    if bool(args.events) != bool(args.output):
        ap.error('--events and --output must be supplied together')
    if args.events:
        events = json.loads(args.events.read_text())
        result = edit(original, events)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('xb') as out:
            out.write(result)
        report = {'source': str(args.input.resolve()), 'source_info': source,
                  'output_info': parse(result), 'events': events,
                  'unit': 'HID pad samples; not game frames',
                  'runtime_validation': 'pending'}
        args.output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
    else:
        print(json.dumps(source, indent=2))


if __name__ == '__main__':
    main()
