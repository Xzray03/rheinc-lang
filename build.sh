#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/src/build_module/build_flags.sh"
BUILD_DIR="$RHEINC_BUILD_DIR"
CXX="$RHEINC_CXX"
CXXFLAGS="$RHEINC_CXXFLAGS"
mkdir -p "$BUILD_DIR/bin" "$BUILD_DIR/obj-cache/RheincMain" "$BUILD_DIR/state/RheincMain"
source "$ROOT_DIR/src/build_module/build_common.sh"

WITH_TESTS=0
MODULE=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --with-tests)
      WITH_TESTS=1
      shift
      ;;
    RhIR|RhLexer|RhPreprocessor|RhParser|RhSemantic|RhAST|RhOptimizer|RhBackend|RhBuiltin|RhLinker)
      MODULE="$1"
      shift
      ;;
    -h|--help)
      cat <<'USAGE'
Usage:
  ./build.sh                 Build root production artifacts (no tests)
  ./build.sh --with-tests    Build root artifacts and module test binaries
  ./build.sh RhLexer         Run module-specific build (includes test binary)
                             Modules: RhIR, RhOptimizer, RhLexer, RhPreprocessor,
                                       RhParser, RhSemantic, RhAST, RhBackend,
                                       RhBuiltin, RhLinker

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

# Build production modules. Test binaries are intentionally not built here unless
# --with-tests is passed explicitly.
modules=(
  RhIR
  RhOptimizer
  RhLexer
  RhPreprocessor
  RhParser
  RhSemantic
  RhAST
  RhBackend
  RhBuiltin
  RhLinker
)

for module in "${modules[@]}"; do
  if [[ "$WITH_TESTS" == "1" ]]; then
    RHEINC_BUILD_TESTS=1 bash "$ROOT_DIR/src/build_module/$module/build.sh"
  else
    RHEINC_BUILD_TESTS=0 bash "$ROOT_DIR/src/build_module/$module/build.sh"
  fi
done

final_source="$ROOT_DIR/src/source/RheincMain.cpp"
final_obj="$BUILD_DIR/obj-cache/RheincMain/RheincMain.o"
final_binary="$BUILD_DIR/bin/rheinc"
final_flags_state="$BUILD_DIR/state/RheincMain/flags"
final_flags_id="$(rh_flags_id "$CXX $CXXFLAGS -I$ROOT_DIR/src/include -DRHEINC_LIBRARY_DIR=$BUILD_DIR/libs/RhBuiltin")"
if [[ ! -f "$final_flags_state" || "$(cat "$final_flags_state")" != "$final_flags_id" ]]; then
  rm -f "$final_obj" "$final_obj.d" "$final_binary"
  echo "$final_flags_id" > "$final_flags_state"
fi

libs=(
  "$BUILD_DIR/libs/rheinc_linker.a"
  "$BUILD_DIR/libs/rheinc_backend.a"
  "$BUILD_DIR/libs/rheinc_ast.a"
  "$BUILD_DIR/libs/rheinc_optimizer.a"
  "$BUILD_DIR/libs/rheinc_ir.a"
  "$BUILD_DIR/libs/rheinc_semantic.a"
  "$BUILD_DIR/libs/rheinc_parser.a"
  "$BUILD_DIR/libs/rheinc_preprocessor.a"
  "$BUILD_DIR/libs/rheinc_lexer.a"
  "$BUILD_DIR/libs/rheinc_core.a"
)

if rh_needs_rebuild "$final_obj" "$final_obj.d" "$final_source"; then
  echo "[CXX] RheincMain.o"
  rh_compile_cxx "$final_source" "$final_obj" \
    -I"$ROOT_DIR/src/include" \
    -DRHEINC_LIBRARY_DIR=\"$BUILD_DIR/libs/RhBuiltin\"
else
  echo "[SKIP] object fresh: RheincMain.o"
fi

relink=0
[[ -f "$final_binary" ]] || relink=1
[[ "$final_obj" -nt "$final_binary" ]] && relink=1
for lib in "${libs[@]}"; do
  [[ "$lib" -nt "$final_binary" ]] && relink=1
done

if [[ "$relink" == "1" ]]; then
  echo "[CXX-LD] $final_binary"
  "$CXX" $CXXFLAGS \
    "$final_obj" \
    "${libs[@]}" \
    -o "$final_binary"
else
  echo "[SKIP] final fresh: rheinc"
fi
echo "[OK] root build selesai: $final_binary"
