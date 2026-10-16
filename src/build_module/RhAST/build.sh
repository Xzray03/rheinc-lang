#!/usr/bin/env bash
set -euo pipefail

MODULE_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd -- "$MODULE_DIR/../../.." && pwd)"

BUILD_TESTS="${RHEINC_BUILD_TESTS:-1}"
RUN_TESTS="${RHEINC_RUN_TESTS:-0}"
if [[ "${1:-}" == "--no-tests" ]]; then
  BUILD_TESTS=0
fi

# RhAST bergantung pada seluruh pipeline front-end; bangun dependency
# (RhSemantic merangkap parser/preprocessor/lexer/core via rekursi).
if [[ "${RHEINC_SKIP_DEPS:-0}" != "1" ]]; then
  (cd "$ROOT_DIR" && bash src/build_module/RhSemantic/build.sh --no-tests)
fi

CXX="${CXX:-g++}"
AR="${AR:-ar}"
CXXFLAGS="${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic -Werror}"
BUILD_DIR="${RHEINC_BUILD_DIR:-$ROOT_DIR/Build}"
OBJ_DIR="$BUILD_DIR/obj-cache/RhAST"
LIB_DIR="$BUILD_DIR/libs"
BIN_TEST_DIR="$BUILD_DIR/bin-test"
STATE_DIR="$BUILD_DIR/state/RhAST"
OBJ_STATE_DIR="$STATE_DIR/objects"

mkdir -p "$OBJ_DIR" "$LIB_DIR" "$BIN_TEST_DIR" "$OBJ_STATE_DIR"

hash_file() {
  [[ -f "$1" ]] || return 1
  cksum "$1" | awk '{print $1}'
}

compute_include_hash() {
  find "$ROOT_DIR/src/include/RhCore" "$ROOT_DIR/src/include/RhLexer" \
    "$ROOT_DIR/src/include/RhPreprocessor" "$ROOT_DIR/src/include/RhParser" \
    "$ROOT_DIR/src/include/RhSemantic" "$ROOT_DIR/src/include/RhAST" \
    -type f -name '*.h' -print0 |
    sort -z |
    xargs -0 cksum |
    cksum |
    awk '{print $1}'
}

FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | cksum | awk '{print $1}')"
INCLUDE_HASH="$(compute_include_hash)"
TEST_SOURCE="$ROOT_DIR/src/source/RhAST/AstIrDumpTool.cpp"
TEST_BINARY="$BIN_TEST_DIR/rheinc_ast_test"
AST_ARCHIVE="$LIB_DIR/rheinc_ast.a"
AST_ARCHIVE_STATE="$STATE_DIR/rheinc_ast.a.sum"
TEST_STATE="$STATE_DIR/rheinc_ast_test.sum"
FLAGS_STATE="$STATE_DIR/flags.id"

DID_WORK=0
OBJECTS_REBUILT=0
ARCHIVES_REBUILT=0

ast_sources=(
  src/source/RhAST/AstToIrTranslator.cpp
  src/source/RhAST/ValidateAst.cpp
  src/source/RhAST/CollectAddressTaken.cpp
  src/source/RhAST/MapSemanticType.cpp
  src/source/RhAST/EncodeConstant.cpp
  src/source/RhAST/LowerExpr.cpp
  src/source/RhAST/LowerMemberAccess.cpp
  src/source/RhAST/LowerFunction.cpp
  src/source/RhAST/LowerStmt.cpp
  src/source/RhAST/LowerIfStatement.cpp
  src/source/RhAST/LowerWhileStatement.cpp
  src/source/RhAST/LowerTranslationUnit.cpp
  src/source/RhAST/InsertPhiAtJoin.cpp
  src/source/RhAST/ExpandDefer.cpp
  src/source/RhAST/IrBuilder.cpp
  src/source/RhAST/IrDump.cpp
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
  local state_file="$OBJ_STATE_DIR/$(state_key_for_source "$source_file").sum"

  local current_object_hash=""
  local current_source_hash=""
  local stored_object_hash=""
  local stored_source_hash=""
  local stored_include_hash=""
  local stored_flags_id=""

  if [[ -f "$object_file" ]]; then
    current_object_hash="$(hash_file "$object_file")"
  fi
  current_source_hash="$(hash_file "$source_path")"

  if [[ -f "$state_file" ]]; then
    read -r stored_object_hash stored_source_hash stored_include_hash stored_flags_id < "$state_file" || true
  fi

  if [[ -n "$current_object_hash" && \
        "$current_object_hash" == "$stored_object_hash" && \
        "$current_source_hash" == "$stored_source_hash" && \
        "$INCLUDE_HASH" == "$stored_include_hash" && \
        "$FLAGS_ID" == "$stored_flags_id" ]]; then
    echo "[SKIP] object fresh: $source_file"
    return 0
  fi

  echo "[CXX] $source_file"
  rm -f "$object_file"
  "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" \
    -c "$source_path" \
    -o "$object_file"

  current_object_hash="$(hash_file "$object_file")"
  printf '%s %s %s %s\n' \
    "$current_object_hash" "$current_source_hash" "$INCLUDE_HASH" "$FLAGS_ID" \
    > "$state_file"

  OBJECTS_REBUILT=1
  DID_WORK=1
}

ensure_archive() {
  local archive_file="$1"
  local state_file="$2"
  shift 2
  local objects=("$@")

  local archive_hash=""
  local stored_archive_hash=""
  if [[ -f "$archive_file" ]]; then
    archive_hash="$(hash_file "$archive_file")"
  fi
  if [[ -f "$state_file" ]]; then
    stored_archive_hash="$(cat "$state_file")"
  fi

  if [[ "$OBJECTS_REBUILT" == "0" && -n "$archive_hash" && \
        "$archive_hash" == "$stored_archive_hash" && \
        -n "$stored_archive_hash" ]] && "$AR" t "$archive_file" >/dev/null 2>&1; then
    echo "[SKIP] archive fresh: $(basename "$archive_file")"
    return 0
  fi

  echo "[AR] $archive_file"
  rm -f "$archive_file"
  "$AR" rcs "$archive_file" "${objects[@]}"
  hash_file "$archive_file" > "$state_file"
  ARCHIVES_REBUILT=1
  DID_WORK=1
}

ast_objects=()
for source_file in "${ast_sources[@]}"; do
  object_file="$OBJ_DIR/$(basename "$source_file" .cpp).o"
  ast_objects+=("$object_file")
  process_object "$source_file" "$object_file"
done

ensure_archive "$AST_ARCHIVE" "$AST_ARCHIVE_STATE" "${ast_objects[@]}"

if [[ "$BUILD_TESTS" == "1" ]]; then
  SEMANTIC_ARCHIVE="$LIB_DIR/rheinc_semantic.a"
  PARSER_ARCHIVE="$LIB_DIR/rheinc_parser.a"
  PREPROCESSOR_ARCHIVE="$LIB_DIR/rheinc_preprocessor.a"
  LEXER_ARCHIVE="$LIB_DIR/rheinc_lexer.a"
  CORE_ARCHIVE="$LIB_DIR/rheinc_core.a"

  current_test_source_hash="$(hash_file "$TEST_SOURCE")"
  current_ast_archive_hash="$(hash_file "$AST_ARCHIVE")"
  current_semantic_archive_hash="$(hash_file "$SEMANTIC_ARCHIVE")"
  current_parser_archive_hash="$(hash_file "$PARSER_ARCHIVE")"
  current_preprocessor_archive_hash="$(hash_file "$PREPROCESSOR_ARCHIVE")"
  current_lexer_archive_hash="$(hash_file "$LEXER_ARCHIVE")"
  current_core_archive_hash="$(hash_file "$CORE_ARCHIVE")"
  current_test_binary_hash=""
  stored_test_binary_hash=""

  if [[ -f "$TEST_BINARY" ]]; then
    current_test_binary_hash="$(hash_file "$TEST_BINARY")"
  fi
  if [[ -f "$TEST_STATE" ]]; then
    stored_test_binary_hash="$(cat "$TEST_STATE")"
  fi

  if [[ "$ARCHIVES_REBUILT" == "0" && -n "$current_test_binary_hash" && \
        "$current_test_binary_hash" == "$stored_test_binary_hash" ]]; then
    echo "[SKIP] test binary fresh: $(basename "$TEST_BINARY")"
  else
    tmp_binary="$BIN_TEST_DIR/.rheinc_ast_test.$$"
    rm -f "$TEST_BINARY" "$tmp_binary"
    echo "[CXX-LD] $TEST_BINARY"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" \
      "$TEST_SOURCE" \
      "$AST_ARCHIVE" \
      "$SEMANTIC_ARCHIVE" \
      "$PARSER_ARCHIVE" \
      "$PREPROCESSOR_ARCHIVE" \
      "$LEXER_ARCHIVE" \
      "$CORE_ARCHIVE" \
      -o "$tmp_binary"
    mv -f "$tmp_binary" "$TEST_BINARY"
    hash_file "$TEST_BINARY" > "$TEST_STATE"
    DID_WORK=1
  fi

  if [[ "$RUN_TESTS" == "1" && "$DID_WORK" == "1" ]]; then
    tmp_run_binary="$(mktemp /tmp/rheinc_ast_test.XXXXXX)"
    cp "$TEST_BINARY" "$tmp_run_binary"
    chmod +x "$tmp_run_binary"
    echo "[TEST] Tier 1 corpus via rheinc_ast_test"
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/halo_dunia.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/integer.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/teks.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/percabangan.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/perulangan.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/fungsi_dan_prosedur.rhc >/dev/null)
    (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/struktur.rhc >/dev/null)
    echo "[TEST] expected-failure: circular_include (harus exit 1)"
    if (cd "$ROOT_DIR" && "$tmp_run_binary" sintaks-awal/rhpp/circular_include.rhc >/dev/null 2>&1); then
      echo "[TEST] GAGAL: circular_include seharusnya exit 1" >&2
      rm -f "$tmp_run_binary"
      exit 1
    fi
    rm -f "$tmp_run_binary"
  fi
fi

if [[ "$DID_WORK" == "0" ]]; then
  echo "[OK] nothing to do"
else
  echo "[OK] RhAST build selesai"
  echo "  libs: $AST_ARCHIVE"
  if [[ "$BUILD_TESTS" == "1" ]]; then
    echo "  bin-test: $TEST_BINARY"
  fi
fi
