#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${BUILD_DIR:-"$project_dir/build"}
mkdir -p "$build_dir"
cd "$build_dir"
"${QMAKE:-qmake}" "$project_dir/MyOverFlow.pro"
make -j"${JOBS:-2}"
