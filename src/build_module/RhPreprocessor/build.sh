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
OBJ_DIR="${RHEINC_OBJ_CACHE_DIR:-$BUILD_DIR/obj-cache/RhPreprocessor}"
LIB_DIR="$BUILD_DIR/libs"
BIN_TEST_DIR="$BUILD_DIR/bin-test"
STATE_DIR="$BUILD_DIR/state/RhPreprocessor"
OBJ_STATE_DIR="$STATE_DIR/objects"

source "$ROOT_DIR/src/build_module/build_common.sh"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$BIN_TEST_DIR" "$OBJ_STATE_DIR"

# RhPreprocessor depends on RhCore and RhLexer production archives.
RHEINC_BUILD_TESTS=0 bash "$ROOT_DIR/src/build_module/RhLexer/build.sh"

hash_file() {
  [[ -f "$1" ]] || return 1
  cksum "$1" | awk '{print $1}'
}

compute_include_hash() {
  find "$ROOT_DIR/src/include/RhCore" "$ROOT_DIR/src/include/RhLexer" "$ROOT_DIR/src/include/RhPreprocessor" \
    -type f -name '*.h' -print0 |
    sort -z |
    xargs -0 cksum |
    cksum |
    awk '{print $1}'
}

FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | cksum | awk '{print $1}')"
INCLUDE_HASH=""
TEST_SOURCE="$ROOT_DIR/src/DumpTool/RhPreprocessor/PreprocessorDumpTool.cpp"
TEST_BINARY="$BIN_TEST_DIR/rheinc_preprocessor_test"
PREPROCESSOR_ARCHIVE="$LIB_DIR/rheinc_preprocessor.a"
PREPROCESSOR_ARCHIVE_STATE="$STATE_DIR/rheinc_preprocessor.a.sum"
TEST_STATE="$STATE_DIR/rheinc_preprocessor_test.sum"
FLAGS_STATE="$STATE_DIR/flags.id"
CORE_ARCHIVE="$LIB_DIR/rheinc_core.a"
LEXER_ARCHIVE="$LIB_DIR/rheinc_lexer.a"

DID_WORK=0
OBJECTS_REBUILT=0
ARCHIVES_REBUILT=0

sources=(
  src/source/RhPreprocessor/MacroTable.cpp
  src/source/RhPreprocessor/ConditionalStack.cpp
  src/source/RhPreprocessor/IncludeState.cpp
  src/source/RhPreprocessor/HeaderSearch.cpp
  src/source/RhPreprocessor/Preprocessor.cpp
  src/source/RhPreprocessor/DirectiveParser.cpp
  src/source/RhPreprocessor/PreprocessorTokenUtil.cpp
  src/source/RhPreprocessor/PreprocessorExpression.cpp
  src/source/RhPreprocessor/MacroExpander.cpp
)

if [[ -f "$FLAGS_STATE" ]] && [[ "$(cat "$FLAGS_STATE")" != "$FLAGS_ID" ]]; then
  echo "[INFO] compiler flags berubah; invalidate state lama"
  rm -rf "$STATE_DIR"
  mkdir -p "$OBJ_STATE_DIR"
fi
printf '%s\n' "$FLAGS_ID" > "$FLAGS_STATE"

state_key_for_source() {
  printf '%s' "$1" | tr '/.' '__'
}

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

archive_hash=""
stored_archive_hash=""
if [[ -f "$PREPROCESSOR_ARCHIVE" ]]; then
  archive_hash="$(hash_file "$PREPROCESSOR_ARCHIVE")"
fi
if [[ -f "$PREPROCESSOR_ARCHIVE_STATE" ]]; then
  stored_archive_hash="$(cat "$PREPROCESSOR_ARCHIVE_STATE")"
fi

if [[ "$OBJECTS_REBUILT" == "0" && -n "$archive_hash" && \
      "$archive_hash" == "$stored_archive_hash" && \
      -n "$stored_archive_hash" ]] && "$AR" t "$PREPROCESSOR_ARCHIVE" >/dev/null 2>&1; then
  echo "[SKIP] archive fresh: $(basename "$PREPROCESSOR_ARCHIVE")"
else
  echo "[AR] $PREPROCESSOR_ARCHIVE"
  rm -f "$PREPROCESSOR_ARCHIVE"
  "$AR" rcs "$PREPROCESSOR_ARCHIVE" "${objects[@]}"
  hash_file "$PREPROCESSOR_ARCHIVE" > "$PREPROCESSOR_ARCHIVE_STATE"
  ARCHIVES_REBUILT=1
  DID_WORK=1
fi

if [[ "$BUILD_TESTS" == "1" ]]; then
  current_test_source_hash="$(hash_file "$TEST_SOURCE")"
  current_preprocessor_hash="$(hash_file "$PREPROCESSOR_ARCHIVE")"
  current_lexer_hash="$(hash_file "$LEXER_ARCHIVE")"
  current_core_hash="$(hash_file "$CORE_ARCHIVE")"
  current_test_binary_hash=""
  stored_test_binary_hash=""
  stored_test_source_hash=""
  stored_preprocessor_hash=""
  stored_lexer_hash=""
  stored_core_hash=""
  stored_test_flags_id=""

  if [[ -f "$TEST_BINARY" ]]; then
    current_test_binary_hash="$(hash_file "$TEST_BINARY")"
  fi
  if [[ -f "$TEST_STATE" ]]; then
    read -r stored_test_binary_hash stored_test_source_hash stored_preprocessor_hash \
      stored_lexer_hash stored_core_hash stored_test_flags_id < "$TEST_STATE" || true
  fi

  if [[ "$ARCHIVES_REBUILT" == "0" && -n "$current_test_binary_hash" && \
        "$current_test_binary_hash" == "$stored_test_binary_hash" && \
        "$current_test_source_hash" == "$stored_test_source_hash" && \
        "$current_preprocessor_hash" == "$stored_preprocessor_hash" && \
        "$current_lexer_hash" == "$stored_lexer_hash" && \
        "$current_core_hash" == "$stored_core_hash" && \
        "$FLAGS_ID" == "$stored_test_flags_id" ]]; then
    echo "[SKIP] test binary fresh: $(basename "$TEST_BINARY")"
  else
    tmp_binary="$BIN_TEST_DIR/.rheinc_preprocessor_test.$$"
    rm -f "$TEST_BINARY" "$tmp_binary"
    echo "[CXX-LD] $TEST_BINARY"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" \
      "$TEST_SOURCE" \
      "$PREPROCESSOR_ARCHIVE" \
      "$LEXER_ARCHIVE" \
      "$CORE_ARCHIVE" \
      -o "$tmp_binary"
    mv -f "$tmp_binary" "$TEST_BINARY"

    current_test_binary_hash="$(hash_file "$TEST_BINARY")"
    printf '%s %s %s %s %s %s\n' \
      "$current_test_binary_hash" "$current_test_source_hash" \
      "$current_preprocessor_hash" "$current_lexer_hash" "$current_core_hash" "$FLAGS_ID" \
      > "$TEST_STATE"
    DID_WORK=1
  fi

  if [[ "$RUN_TESTS" == "1" && "$DID_WORK" == "1" ]]; then
    tmp_run_binary="$(mktemp /tmp/rheinc_preprocessor_test.XXXXXX)"
    cp "$TEST_BINARY" "$tmp_run_binary"
    chmod +x "$tmp_run_binary"
    echo "[TEST] $tmp_run_binary sintaks-awal/rhpp/preprocessor.rhc"
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/rhpp/preprocessor.rhc)
    rm -f "$tmp_run_binary"
  fi
fi

if [[ "$DID_WORK" == "0" ]]; then
  echo "[OK] nothing to do"
else
  echo "[OK] RhPreprocessor build selesai"
  echo "  libs: $PREPROCESSOR_ARCHIVE"
  if [[ "$BUILD_TESTS" == "1" ]]; then
    echo "  bin-test: $TEST_BINARY"
  fi
fi
