#!/usr/bin/env bash
# build.sh - Modul RhBuiltin: runtime bahasa (per-runtime object).
#
# Tiap sumber runtime dikompilasi menjadi object file TERSENDIRI di
# "$BUILD_DIR/libs/RhBuiltin/" — ini "library RhBuiltin" yang nanti
# diseleksi RhLinker (hanya object yang benar-benar dipakai program).
#
# Pemakaian: RHEINC_BUILD_DIR=<dir> bash build.sh [--no-tests]
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
OBJ_DIR="$BUILD_DIR/obj-cache/RhBuiltin"
LIB_DIR="$BUILD_DIR/libs/RhBuiltin"

mkdir -p "$OBJ_DIR" "$LIB_DIR"

CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic}"

build_cpp() {
  local src="$1" name obj
  name="$(basename "$src")"; name="${name%.cpp}"
  obj="$OBJ_DIR/$name.o"
  if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
    echo "[CXX] $name.o"
    "$CXX" $CXXFLAGS -c "$ROOT_DIR/$src" -o "$obj"
  fi
  # Object per-runtime masuk library RhBuiltin (salinan, mtime preserv).
  if [[ ! -f "$LIB_DIR/$name.o" || "$obj" -nt "$LIB_DIR/$name.o" ]]; then
    cp -p "$obj" "$LIB_DIR/$name.o"
  fi
}

build_s() {
  local src="$1" name obj
  name="$(basename "$src")"; name="${name%.s}"
  obj="$OBJ_DIR/$name.o"
  if [[ ! -f "$obj" || "$src" -nt "$obj" ]]; then
    echo "[AS] $name.o"
    as "$ROOT_DIR/$src" -o "$obj"
  fi
  if [[ ! -f "$LIB_DIR/$name.o" || "$obj" -nt "$LIB_DIR/$name.o" ]]; then
    cp -p "$obj" "$LIB_DIR/$name.o"
  fi
}

for src in "$ROOT_DIR"/src/source/RhBuiltin/*.cpp; do
  [[ -e "$src" ]] && build_cpp "${src#"$ROOT_DIR"/}"
done
for src in "$ROOT_DIR"/src/source/RhBuiltin/*.s; do
  [[ -e "$src" ]] && build_s "${src#"$ROOT_DIR"/}"
done

echo "[OK] RhBuiltin build selesai (library: $LIB_DIR)"
