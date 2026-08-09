#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
build_dir=${1:-"$project_root/build-package"}
output_dir=${2:-"$project_root/dist"}

if [[ $(uname -m) != x86_64 ]]; then
  printf 'Modern IME Debian packages currently support x86_64 only.\n' >&2
  exit 2
fi

cmake -S "$project_root" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr \
  -DMODERN_IME_ENABLE_DEB_PACKAGING=ON \
  -DMODERN_IME_INSTALL_TEST_TOOLS=OFF \
  -DMODERN_IME_WARNINGS_AS_ERRORS=ON
cmake --build "$build_dir" --parallel
ctest --test-dir "$build_dir" --output-on-failure

mkdir -p "$output_dir"
cpack --config "$build_dir/CPackConfig.cmake" -G DEB -B "$output_dir"
printf 'Debian package written to %s\n' "$output_dir"
