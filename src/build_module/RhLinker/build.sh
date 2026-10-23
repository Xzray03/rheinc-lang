#!/usr/bin/env bash
# build.sh - Modul RhLinker.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
source "$ROOT_DIR/src/build_module/build_flags.sh"
BUILD_DIR="$RHEINC_BUILD_DIR"
OBJ_DIR="$BUILD_DIR/obj-cache/RhLinker"
LIB_DIR="$BUILD_DIR/libs"
STATE_DIR="$BUILD_DIR/state/RhLinker"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$STATE_DIR"
source "$ROOT_DIR/src/build_module/build_common.sh"

CXX="$RHEINC_CXX"
AR="$RHEINC_AR"
CXXFLAGS="$RHEINC_CXXFLAGS"
ARCHIVE="$LIB_DIR/rheinc_linker.a"
FLAGS_STATE="$STATE_DIR/flags"
FLAGS_ID="$(rh_flags_id "$CXX $CXXFLAGS")"
if [[ ! -f "$FLAGS_STATE" || "$(cat "$FLAGS_STATE")" != "$FLAGS_ID" ]]; then
  rm -f "$OBJ_DIR"/*.o "$OBJ_DIR"/*.d
  echo "$FLAGS_ID" > "$FLAGS_STATE"
fi

sources=(
  src/source/RhLinker/RhLinker.cpp
)
objs=()
for src in "${sources[@]}"; do
  name="$(basename "$src")"; name="${name%.cpp}"
  obj="$OBJ_DIR/$name.o"
  if rh_needs_rebuild "$obj" "$obj.d" "$ROOT_DIR/$src"; then
    echo "[CXX] $name.o"
    rh_compile_cxx "$ROOT_DIR/$src" "$obj" -I"$ROOT_DIR/src/include"
  else
    echo "[SKIP] object fresh: $name.o"
  fi
  objs+=("$obj")
done

archive_state="$STATE_DIR/rheinc_linker.a.members"
if rh_archive_fresh "$ARCHIVE" "$archive_state" "${objs[@]}"; then
  echo "[SKIP] archive fresh: rheinc_linker.a"
else
  rh_update_archive "$ARCHIVE" "$archive_state" "${objs[@]}"
fi
