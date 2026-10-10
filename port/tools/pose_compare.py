#!/usr/bin/env python3
"""Compare captured source poses; the caller must establish matching phases.

This compares animation state, local bones and final bone matrices separately.
Addresses, observer clocks and call counters are descriptive metadata and are
not pose fields. A missing field is a capture error, never an equality result.
"""
import argparse
import json
from pathlib import Path

MODEL_FIELDS = {
    'slot', 'num_bones', 'world_matrix_fx12', 'num_frames_and_flags',
    'cursor_fx12', 'speed_fx12', 'animation_file', 'secondary_file',
    'secondary_num_frames_and_flags', 'secondary_cursor_fx12',
    'secondary_speed_fx12', 'local_bones', 'bone_matrices_fx12',
}


def differences(first, second, path=''):
    if type(first) is not type(second):
        return [{'path': path, 'first': first, 'second': second}]
    if isinstance(first, dict):
        out = []
        for key in sorted(first.keys() | second.keys()):
            child = f'{path}.{key}' if path else key
            if key not in first or key not in second:
                out.append({'path': child, 'missing_from': 'first' if key not in first else 'second'})
            else:
                out.extend(differences(first[key], second[key], child))
        return out
    if isinstance(first, list):
        if len(first) != len(second):
            return [{'path': path, 'first_length': len(first), 'second_length': len(second)}]
        return [item for i, (a, b) in enumerate(zip(first, second))
                for item in differences(a, b, f'{path}[{i}]')]
    return [] if first == second else [{'path': path, 'first': first, 'second': second}]


def validate(pose):
    if pose.get('schema') != 'sm64ds-pose-telemetry-v1':
        raise ValueError('Unsupported or missing pose schema')
    for field in ('body_model_id', 'pose_controls', 'models'):
        if field not in pose:
            raise ValueError(f'Missing pose field {field}')
    if len(pose['models']) != 4:
        raise ValueError('Expected four body model slots')
    controls = pose['pose_controls']
    if set(controls) != {'hex_742', 'hex_743', 'hex_75c_766'} or len(controls['hex_75c_766']) != 6:
        raise ValueError('Incomplete pose controls')
    if not any(model is not None for model in pose['models']):
        raise ValueError('No body model captured')
    for i, model in enumerate(pose['models']):
        if model is None:
            continue
        missing = MODEL_FIELDS - model.keys()
        if missing:
            raise ValueError(f'Model {i} missing {sorted(missing)}')
        if model['slot'] != i or len(model['local_bones']) != model['num_bones'] or len(model['bone_matrices_fx12']) != model['num_bones']:
            raise ValueError(f'Model {i} inconsistent bone capture')
        if len(model['world_matrix_fx12']) != 12 or any(len(matrix) != 12 for matrix in model['bone_matrices_fx12']):
            raise ValueError(f'Model {i} incomplete matrix capture')
        for bone in model['local_bones']:
            if set(bone) != {'scale_fx12', 'flags', 'rotation_u16', 'translation_fx12', 'override_file', 'override_frame'}:
                raise ValueError(f'Model {i} incomplete local bone capture')
            if any(len(bone[key]) != 3 for key in ('scale_fx12', 'rotation_u16', 'translation_fx12')):
                raise ValueError(f'Model {i} incomplete bone vector')


def compare(first, second):
    errors = []
    for name, pose in [('first', first), ('second', second)]:
        try:
            validate(pose)
        except (ValueError, TypeError, KeyError) as exc:
            errors.append(f'{name}: {exc}')
    if errors:
        return {'capture_complete': False, 'pose_equal': False, 'errors': errors}
    a = {key: first[key] for key in ('body_model_id', 'pose_controls', 'models')}
    b = {key: second[key] for key in a}
    diff = differences(a, b)
    categories = {'animation': [], 'local_bones': [], 'bone_matrices': [], 'world_matrix': [], 'controls': []}
    for row in diff:
        path = row['path']
        group = ('local_bones' if '.local_bones' in path else
                 'bone_matrices' if '.bone_matrices_fx12' in path else
                 'world_matrix' if '.world_matrix_fx12' in path else
                 'controls' if not path.startswith('models') else 'animation')
        categories[group].append(row)
    return {'schema': 'sm64ds-pose-comparison-v1', 'capture_complete': True,
            'pose_equal': not diff, 'phase_matching_required': True,
            'first_phase': first.get('phase'), 'second_phase': second.get('phase'),
            'difference_counts': {k: len(v) for k, v in categories.items()},
            'differences': categories}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('first', type=Path)
    parser.add_argument('second', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = compare(json.loads(args.first.read_text()), json.loads(args.second.read_text()))
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(output)
    else:
        print(output, end='')
