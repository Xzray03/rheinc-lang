#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$MODULE_DIR/../../.." && pwd)"

CXX="${CXX:-g++}"
AR="${AR:-ar}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic -Werror}"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
OBJ_DIR="$BUILD_DIR/obj-cache/RhIR"
LIB_DIR="$BUILD_DIR/libs"
STATE_DIR="$BUILD_DIR/state/RhIR"
ARCHIVE="$LIB_DIR/rheinc_ir.a"
FLAGS_STATE="$STATE_DIR/flags"

source "$ROOT_DIR/src/build_module/build_common.sh"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$STATE_DIR"

hash_file() {
  cksum | awk '{print $1}'
}

FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | hash_file)"
if [[ -f "$FLAGS_STATE" ]] && [[ "$(cat "$FLAGS_STATE")" == "$FLAGS_ID" ]]; then
  :
else
  rm -f "$OBJ_DIR"/*.o "$OBJ_DIR"/*.d
  printf '%s' "$FLAGS_ID" > "$FLAGS_STATE"
fi

sources=(
  src/source/RhIR/IrBuilder.cpp
  src/source/RhIR/IrDump.cpp
  src/source/RhIR/IrEditor.cpp
  src/source/RhIR/IrTargetId.cpp
  src/source/RhIR/SystemVAmd64Provider.cpp
  src/source/RhIR/IrProviderManager.cpp
  src/source/RhIR/IrLayoutEngine.cpp
  src/source/RhIR/IrVerifier.cpp
  src/source/RhIR/FinalIr.cpp
  src/source/RhIR/IrManager.cpp
  src/source/RhIR/IrLinker.cpp
)

objects=()
rebuilt=0
for relsrc in "${sources[@]}"; do
  obj="$OBJ_DIR/$(basename "${relsrc%.cpp}").o"
  objects+=("$obj")

  if rh_needs_rebuild "$obj" "$obj.d" "$ROOT_DIR/$relsrc"; then
    echo "[CXX] $relsrc"
    rh_compile_cxx "$ROOT_DIR/$relsrc" "$obj" -I"$ROOT_DIR/src/include"
    rebuilt=1
  else
    echo "[SKIP] object fresh: $(basename "$obj")"
  fi
done

archive_state="$STATE_DIR/$(basename "$ARCHIVE").members"
if rh_archive_fresh "$ARCHIVE" "$archive_state" "${objects[@]}"; then
  echo "[SKIP] archive fresh: $(basename "$ARCHIVE")"
else
  rh_update_archive "$ARCHIVE" "$archive_state" "${objects[@]}"
fi

echo "[OK] RhIR build selesai"
echo "  libs: $ARCHIVE"
