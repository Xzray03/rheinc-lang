#!/usr/bin/env bash
# build.sh - Modul RhLinker.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
OBJ_DIR="$BUILD_DIR/obj-cache/RhLinker"
LIB_DIR="$BUILD_DIR/libs"
STATE_DIR="$BUILD_DIR/state"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$STATE_DIR"
CXX="${CXX:-g++}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic}"
ARCHIVE="$LIB_DIR/rheinc_linker.a"
sources=(
  src/source/RhLinker/RhLinker.cpp
)
objs=()
for src in "${sources[@]}"; do
  name="$(basename "$src")"; name="${name%.cpp}"
  obj="$OBJ_DIR/$name.o"
  if [[ ! -f "$obj" || "$ROOT_DIR/$src" -nt "$obj" ]]; then
    echo "[CXX] $name.o"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" -c "$ROOT_DIR/$src" -o "$obj"
  fi
  objs+=("$obj")
done
archive_state="$STATE_DIR/rheinc_linker.a.cksum"
new_cksum="$(cat "${objs[@]}" | cksum)"
if [[ ! -f "$ARCHIVE" || ! -f "$archive_state" || "$(cat "$archive_state")" != "$new_cksum" ]]; then
  echo "[AR] rheinc_linker.a"
  ar rcs "$ARCHIVE" "${objs[@]}"
  echo "$new_cksum" > "$archive_state"
else
  echo "[SKIP] archive fresh: rheinc_linker.a"
fi
