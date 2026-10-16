#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec "${MONITOR_BINARY:-$project_dir/build/MyOverFlow}" --config "${MONITOR_CONFIG:-$project_dir/config/monitor.json}" "$@"
