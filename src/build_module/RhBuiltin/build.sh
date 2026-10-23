#!/usr/bin/env bash
# build.sh - Modul RhBuiltin: runtime bahasa (per-runtime object).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
source "$ROOT_DIR/src/build_module/build_flags.sh"  # build_flags dulu: set BUILD_DIR via RHEINC_BUILD_DIR
BUILD_DIR="$RHEINC_BUILD_DIR"
OBJ_DIR="$BUILD_DIR/obj-cache/RhBuiltin"
LIB_DIR="$BUILD_DIR/libs/RhBuiltin"
STATE_DIR="$BUILD_DIR/state/RhBuiltin"
CXX="$RHEINC_CXX"
CXXFLAGS="$RHEINC_CXXFLAGS"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$STATE_DIR"
source "$ROOT_DIR/src/build_module/build_common.sh"

FLAGS_STATE="$STATE_DIR/flags"
FLAGS_ID="$(rh_flags_id "$CXX $CXXFLAGS")"
if [[ ! -f "$FLAGS_STATE" || "$(cat "$FLAGS_STATE")" != "$FLAGS_ID" ]]; then
  rm -f "$OBJ_DIR"/*.o "$OBJ_DIR"/*.d
  echo "$FLAGS_ID" > "$FLAGS_STATE"
fi

members=()

build_cpp() {
  local src="$1" name obj
  name="$(basename "$src")"; name="${name%.cpp}"
  obj="$OBJ_DIR/$name.o"
  if rh_needs_rebuild "$obj" "$obj.d" "$ROOT_DIR/$src"; then
    echo "[CXX] $name.o"
    rh_compile_cxx "$ROOT_DIR/$src" "$obj" -I"$ROOT_DIR/src/include"
  else
    echo "[SKIP] object fresh: $name.o"
  fi
  if [[ ! -f "$LIB_DIR/$name.o" || "$obj" -nt "$LIB_DIR/$name.o" ]]; then
    cp -p "$obj" "$LIB_DIR/$name.o"
  fi
  members+=("$name.o")
}

build_s() {
  local src="$1" name obj
  name="$(basename "$src")"; name="${name%.s}"
  obj="$OBJ_DIR/$name.o"
  if [[ ! -f "$obj" || "$ROOT_DIR/$src" -nt "$obj" ]]; then
    echo "[AS] $name.o"
    as "$ROOT_DIR/$src" -o "$obj"
  fi
  if [[ ! -f "$LIB_DIR/$name.o" || "$obj" -nt "$LIB_DIR/$name.o" ]]; then
    cp -p "$obj" "$LIB_DIR/$name.o"
  fi
  members+=("$name.o")
}

for src in "$ROOT_DIR"/src/source/RhBuiltin/*.cpp; do
  [[ -e "$src" ]] && build_cpp "${src#"$ROOT_DIR"/}"
done
for src in "$ROOT_DIR"/src/source/RhBuiltin/*.s; do
  [[ -e "$src" ]] && build_s "${src#"$ROOT_DIR"/}"
done

# Bersihkan object runtime yang sumbernya sudah dihapus.
printf '%s\n' "${members[@]}" > "$STATE_DIR/members"
for obj in "$LIB_DIR"/*.o; do
  [[ -e "$obj" ]] || continue
  if ! grep -qxF "$(basename "$obj")" "$STATE_DIR/members"; then
    echo "[RM] stale runtime: $(basename "$obj")"
    rm -f "$obj" "$OBJ_DIR/$(basename "$obj")"
  fi
done

echo "[OK] RhBuiltin build selesai (library: $LIB_DIR)"
