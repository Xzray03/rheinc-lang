#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$MODULE_DIR/../../.." && pwd)"

BUILD_TESTS="${RHEINC_BUILD_TESTS:-1}"
RUN_TESTS="${RHEINC_RUN_TESTS:-0}"

source "$ROOT_DIR/src/build_module/build_flags.sh"
CXX="$RHEINC_CXX"
AR="$RHEINC_AR"
CXXFLAGS="$RHEINC_CXXFLAGS"
BUILD_DIR="$RHEINC_BUILD_DIR"
OBJ_DIR="${RHEINC_OBJ_CACHE_DIR:-$BUILD_DIR/obj-cache/RhParser}"
LIB_DIR="$BUILD_DIR/libs"
BIN_TEST_DIR="$BUILD_DIR/bin-test"
STATE_DIR="$BUILD_DIR/state/RhParser"
OBJ_STATE_DIR="$STATE_DIR/objects"
source "$ROOT_DIR/src/build_module/build_common.sh"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$BIN_TEST_DIR" "$OBJ_STATE_DIR"

# Dependencies: RhCore + RhLexer + RhPreprocessor production archives.
RHEINC_BUILD_TESTS=0 bash "$ROOT_DIR/src/build_module/RhPreprocessor/build.sh"

hash_file() { [[ -f "$1" ]] || return 1; cksum "$1" | awk '{print $1}'; }
compute_include_hash() {
  find "$ROOT_DIR/src/include/RhCore" "$ROOT_DIR/src/include/RhLexer" "$ROOT_DIR/src/include/RhPreprocessor" "$ROOT_DIR/src/include/RhParser" \
    -type f -name '*.h' -print0 | sort -z | xargs -0 cksum | cksum | awk '{print $1}'
}
FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | cksum | awk '{print $1}')"
INCLUDE_HASH=""
FLAGS_STATE="$STATE_DIR/flags.id"
PARSER_ARCHIVE="$LIB_DIR/rheinc_parser.a"
PARSER_ARCHIVE_STATE="$STATE_DIR/rheinc_parser.a.sum"
TEST_SOURCE="$ROOT_DIR/src/DumpTool/RhParser/ParserDumpTool.cpp"
TEST_BINARY="$BIN_TEST_DIR/rheinc_parser_test"
TEST_STATE="$STATE_DIR/rheinc_parser_test.sum"
CORE_ARCHIVE="$LIB_DIR/rheinc_core.a"
LEXER_ARCHIVE="$LIB_DIR/rheinc_lexer.a"
PP_ARCHIVE="$LIB_DIR/rheinc_preprocessor.a"

DID_WORK=0
OBJECTS_REBUILT=0
ARCHIVES_REBUILT=0

sources=(
  src/source/RhParser/Parser.cpp
  src/source/RhParser/ParserTokenStream.cpp
  src/source/RhParser/ParseDecl.cpp
  src/source/RhParser/ParseType.cpp
  src/source/RhParser/ParseStmt.cpp
  src/source/RhParser/ParseExpr.cpp
)

if [[ -f "$FLAGS_STATE" ]] && [[ "$(cat "$FLAGS_STATE")" != "$FLAGS_ID" ]]; then
  echo "[INFO] compiler flags berubah; invalidate state lama"
  rm -rf "$STATE_DIR"
  mkdir -p "$OBJ_STATE_DIR"
fi
printf '%s\n' "$FLAGS_ID" > "$FLAGS_STATE"

key() { printf '%s' "$1" | tr '/.' '__'; }
process_object() {
  local source_file="$1"
  local object_file="$2"
  local source_path="$ROOT_DIR/$source_file"

  if rh_needs_rebuild "$object_file" "$object_file.d" "$source_path"; then
    echo "[CXX] $source_file"
    rh_compile_cxx "$source_path" "$object_file" -I"$ROOT_DIR/src/include"
    OBJECTS_REBUILT=1
    DID_WORK=1
  else
    echo "[SKIP] object fresh: $source_file"
  fi
}

objects=()
for source_file in "${sources[@]}"; do
  object_file="$OBJ_DIR/$(basename "$source_file" .cpp).o"
  objects+=("$object_file")
  process_object "$source_file" "$object_file"
done

archive_hash=""; stored_archive_hash=""
[[ -f "$PARSER_ARCHIVE" ]] && archive_hash="$(hash_file "$PARSER_ARCHIVE")"
[[ -f "$PARSER_ARCHIVE_STATE" ]] && stored_archive_hash="$(cat "$PARSER_ARCHIVE_STATE")"
if [[ "$OBJECTS_REBUILT" == "0" && -n "$archive_hash" && "$archive_hash" == "$stored_archive_hash" ]] && "$AR" t "$PARSER_ARCHIVE" >/dev/null 2>&1; then
  echo "[SKIP] archive fresh: $(basename "$PARSER_ARCHIVE")"
else
  echo "[AR] $PARSER_ARCHIVE"
  rm -f "$PARSER_ARCHIVE"
  "$AR" rcs "$PARSER_ARCHIVE" "${objects[@]}"
  hash_file "$PARSER_ARCHIVE" > "$PARSER_ARCHIVE_STATE"
  ARCHIVES_REBUILT=1
  DID_WORK=1
fi

if [[ "$BUILD_TESTS" == "1" ]]; then
  test_src_hash="$(hash_file "$TEST_SOURCE")"
  parser_hash="$(hash_file "$PARSER_ARCHIVE")"
  pp_hash="$(hash_file "$PP_ARCHIVE")"
  lexer_hash="$(hash_file "$LEXER_ARCHIVE")"
  core_hash="$(hash_file "$CORE_ARCHIVE")"
  bin_hash=""; sb=""; ss=""; sp=""; spp=""; sl=""; sc=""; sf=""
  [[ -f "$TEST_BINARY" ]] && bin_hash="$(hash_file "$TEST_BINARY")"
  [[ -f "$TEST_STATE" ]] && read -r sb ss sp spp sl sc sf < "$TEST_STATE" || true
  if [[ "$ARCHIVES_REBUILT" == "0" && -n "$bin_hash" && "$bin_hash" == "$sb" && "$test_src_hash" == "$ss" && "$parser_hash" == "$sp" && "$pp_hash" == "$spp" && "$lexer_hash" == "$sl" && "$core_hash" == "$sc" && "$FLAGS_ID" == "$sf" ]]; then
    echo "[SKIP] test binary fresh: $(basename "$TEST_BINARY")"
  else
    tmp_binary="$BIN_TEST_DIR/.rheinc_parser_test.$$"
    rm -f "$TEST_BINARY" "$tmp_binary"
    echo "[CXX-LD] $TEST_BINARY"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" "$TEST_SOURCE" "$PARSER_ARCHIVE" "$PP_ARCHIVE" "$LEXER_ARCHIVE" "$CORE_ARCHIVE" -o "$tmp_binary"
    mv -f "$tmp_binary" "$TEST_BINARY"
    bin_hash="$(hash_file "$TEST_BINARY")"
    printf '%s %s %s %s %s %s %s\n' "$bin_hash" "$test_src_hash" "$parser_hash" "$pp_hash" "$lexer_hash" "$core_hash" "$FLAGS_ID" > "$TEST_STATE"
    DID_WORK=1
  fi

  if [[ "$RUN_TESTS" == "1" && "$DID_WORK" == "1" ]]; then
    tmp_run="$(mktemp /tmp/rheinc_parser_test.XXXXXX)"
    cp "$TEST_BINARY" "$tmp_run"; chmod +x "$tmp_run"
    echo "[TEST] $tmp_run sintaks-awal/halo_dunia.rhc"
    (cd "$ROOT_DIR" && "$tmp_run" sintaks-awal/halo_dunia.rhc)
    rm -f "$tmp_run"
  fi
fi

if [[ "$DID_WORK" == "0" ]]; then
  echo "[OK] nothing to do"
else
  echo "[OK] RhParser build selesai"
  echo "  libs: $PARSER_ARCHIVE"
  if [[ "$BUILD_TESTS" == "1" ]]; then echo "  bin-test: $TEST_BINARY"; fi
fi
