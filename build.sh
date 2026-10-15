#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic -Werror}"

WITH_TESTS=0
MODULE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-tests)
      WITH_TESTS=1
      shift
      ;;
    RhLexer)
      MODULE="$1"
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage:
  ./build.sh                 Build root production artifacts (no tests)
  ./build.sh --with-tests    Build root artifacts and module test binaries
  ./build.sh RhLexer         Run module-specific RhLexer build (includes test binary)

Environment:
  CXX=g++
  CXXFLAGS="-std=c++20 -Wall -Wextra -Wpedantic -Werror"
  RHEINC_BUILD_DIR=Build
  RHEINC_RUN_TESTS=1         Run smoke test after module-specific build
USAGE
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 2
      ;;
  esac
done

if [[ -n "$MODULE" ]]; then
  export RHEINC_BUILD_TESTS=1
  exec bash "$ROOT_DIR/src/build_module/$MODULE/build.sh"
fi

mkdir -p "$BUILD_DIR/bin"

# Build production modules. Test binaries are intentionally not built here unless
# --with-tests is passed explicitly.
modules=(
  RhLexer
  RhPreprocessor
  RhParser
)

for module in "${modules[@]}"; do
  if [[ "$WITH_TESTS" == "1" ]]; then
    RHEINC_BUILD_TESTS=1 bash "$ROOT_DIR/src/build_module/$module/build.sh"
  else
    RHEINC_BUILD_TESTS=0 bash "$ROOT_DIR/src/build_module/$module/build.sh" --no-tests
  fi
done

final_source="$ROOT_DIR/src/source/RheincMain.cpp"
final_binary="$BUILD_DIR/bin/rheinc"

if [[ -f "$final_source" ]]; then
  echo "[CXX-LD] $final_binary"
  "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" \
    "$final_source" \
    "$BUILD_DIR/libs/rheinc_parser.a" \
    "$BUILD_DIR/libs/rheinc_preprocessor.a" \
    "$BUILD_DIR/libs/rheinc_lexer.a" \
    "$BUILD_DIR/libs/rheinc_core.a" \
    -o "$final_binary"
  echo "[OK] root build selesai: $final_binary"
else
  echo "[OK] root production libraries selesai"
  echo "[INFO] final binary 'rheinc' belum tersedia; tunggu RhParser/codegen selesai"
fi
