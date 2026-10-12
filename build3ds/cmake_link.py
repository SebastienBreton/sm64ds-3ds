"""Read CMake linker arguments, including Linux response files."""
from pathlib import Path
import shlex


def expand_response_files(arguments, directory, active=()):
    expanded = []
    for argument in arguments:
        if not argument.startswith("@"):
            expanded.append(argument)
            continue
        path = (Path(directory) / argument[1:]).resolve()
        if path in active:
            raise ValueError(f"recursive linker response file: {path}")
        expanded.extend(expand_response_files(shlex.split(path.read_text()), directory, (*active, path)))
    return expanded
