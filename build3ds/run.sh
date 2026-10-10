#!/usr/bin/env bash
set -euo pipefail
D="$(cd "$(dirname "$0")" && pwd)"
exec python3 "$D/run.py" "$1" --timeout "${2:-180}"
