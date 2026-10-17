#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$MODULE_DIR/../../.." && pwd)"

CXX="${CXX:-g++}"
AR="${AR:-ar}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic -Werror}"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
OBJ_DIR="$BUILD_DIR/obj-cache/RhOptimizer"
LIB_DIR="$BUILD_DIR/libs"
STATE_DIR="$BUILD_DIR/state/RhOptimizer"
ARCHIVE="$LIB_DIR/rheinc_optimizer.a"
FLAGS_STATE="$STATE_DIR/flags"

mkdir -p "$OBJ_DIR" "$LIB_DIR" "$STATE_DIR"

hash_file() {
  cksum | awk '{print $1}'
}

FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | hash_file)"
if [[ -f "$FLAGS_STATE" ]] && [[ "$(cat "$FLAGS_STATE")" == "$FLAGS_ID" ]]; then
  :
else
  rm -f "$OBJ_DIR"/*.o
  printf '%s' "$FLAGS_ID" > "$FLAGS_STATE"
fi

sources=(
  src/source/RhOptimizer/ConstantPropagation.cpp
  src/source/RhOptimizer/Cse.cpp
  src/source/RhOptimizer/OptimizerManager.cpp
  src/source/RhOptimizer/PeepholeConstFold.cpp
  src/source/RhOptimizer/StrengthReduction.cpp
  src/source/RhOptimizer/DeadCodeElim.cpp
  src/source/RhOptimizer/Licm.cpp
  src/source/RhOptimizer/LoopUnroll.cpp
  src/source/RhOptimizer/UnreachableBlockElim.cpp
)

objects=()
rebuilt=0
for relsrc in "${sources[@]}"; do
  obj="$OBJ_DIR/$(basename "${relsrc%.cpp}").o"
  objects+=("$obj")

  need_build=0
  if [[ ! -f "$obj" ]]; then
    need_build=1
  elif [[ "$ROOT_DIR/$relsrc" -nt "$obj" ]]; then
    need_build=1
  else
    while IFS= read -r -d '' header; do
      if [[ "$header" -nt "$obj" ]]; then
        need_build=1
        break
      fi
    done < <(find "$ROOT_DIR/src/include/RhOptimizer" "$ROOT_DIR/src/include/RhIR" -name '*.h' -print0)
  fi

  if [[ "$need_build" == "1" ]]; then
    echo "[CXX] $relsrc"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" -c "$ROOT_DIR/$relsrc" -o "$obj"
    rebuilt=1
  else
    echo "[SKIP] object fresh: $(basename "$obj")"
  fi
done

archive_state="$STATE_DIR/rheinc_optimizer.a.cksum"
archive_hash=""
stored_archive_hash=""
if [[ -f "$ARCHIVE" ]]; then
  archive_hash="$(hash_file < "$ARCHIVE")"
fi
if [[ -f "$archive_state" ]]; then
  stored_archive_hash="$(cat "$archive_state")"
fi

if [[ "$rebuilt" == "0" && -n "$archive_hash" && "$archive_hash" == "$stored_archive_hash" ]] && "$AR" t "$ARCHIVE" >/dev/null 2>&1; then
  echo "[SKIP] archive fresh: rheinc_optimizer.a"
else
  echo "[AR] $ARCHIVE"
  rm -f "$ARCHIVE"
  "$AR" rcs "$ARCHIVE" "${objects[@]}"
  hash_file < "$ARCHIVE" > "$archive_state"
fi

echo "[OK] RhOptimizer build selesai"
echo "  libs: $ARCHIVE"
