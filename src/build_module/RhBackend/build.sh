#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$MODULE_DIR/../../.." && pwd)"

source "$ROOT_DIR/src/build_module/build_flags.sh"
CXX="$RHEINC_CXX"
AR="$RHEINC_AR"
CXXFLAGS="$RHEINC_CXXFLAGS"
BUILD_DIR="$RHEINC_BUILD_DIR"
OBJ_DIR="$BUILD_DIR/obj-cache/RhBackend"
LIB_DIR="$BUILD_DIR/libs"
STATE_DIR="$BUILD_DIR/state/RhBackend"
ARCHIVE="$LIB_DIR/rheinc_backend.a"
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
  src/source/RhBackend/AsmBuilder.cpp
  src/source/RhBackend/X86_64SysVEmitter.cpp
  src/source/RhBackend/RhBackend.cpp
  src/source/RhBackend/GraphColoringRegAlloc.cpp
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

echo "[OK] RhBackend build selesai"
echo "  libs: $ARCHIVE"
