#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${TEST_BUILD_DIR:-"$project_dir/build-tests"}
mkdir -p "$build_dir"
cd "$build_dir"
"${QMAKE:-qmake}" "$project_dir/tests/tests.pro"
make -j"${JOBS:-2}"
./monitor_tests
