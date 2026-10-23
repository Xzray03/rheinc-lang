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
OBJ_DIR="${RHEINC_OBJ_CACHE_DIR:-$BUILD_DIR/obj-cache/RhSemantic}"
LIB_DIR="$BUILD_DIR/libs"
BIN_TEST_DIR="$BUILD_DIR/bin-test"
STATE_DIR="$BUILD_DIR/state/RhSemantic"
OBJ_STATE_DIR="$STATE_DIR/objects"
source "$ROOT_DIR/src/build_module/build_common.sh"
mkdir -p "$OBJ_DIR" "$LIB_DIR" "$BIN_TEST_DIR" "$OBJ_STATE_DIR"

# Dependencies: RhCore + RhLexer + RhPreprocessor + RhParser production archives.
RHEINC_BUILD_TESTS=0 bash "$ROOT_DIR/src/build_module/RhParser/build.sh"

hash_file() { [[ -f "$1" ]] || return 1; cksum "$1" | awk '{print $1}'; }
compute_include_hash() {
  find "$ROOT_DIR/src/include/RhCore" "$ROOT_DIR/src/include/RhLexer" \
       "$ROOT_DIR/src/include/RhPreprocessor" "$ROOT_DIR/src/include/RhParser" \
       "$ROOT_DIR/src/include/RhSemantic" \
    -type f -name '*.h' -print0 | sort -z | xargs -0 cksum | cksum | awk '{print $1}'
}
FLAGS_ID="$(printf '%s' "$CXX $AR $CXXFLAGS" | cksum | awk '{print $1}')"
INCLUDE_HASH=""
FLAGS_STATE="$STATE_DIR/flags.id"
SEMANTIC_ARCHIVE="$LIB_DIR/rheinc_semantic.a"
SEMANTIC_ARCHIVE_STATE="$STATE_DIR/rheinc_semantic.a.sum"
TEST_SOURCE="$ROOT_DIR/src/DumpTool/RhSemantic/SemanticDumpTool.cpp"
TEST_BINARY="$BIN_TEST_DIR/rheinc_semantic_test"
TEST_STATE="$STATE_DIR/rheinc_semantic_test.sum"
CORE_ARCHIVE="$LIB_DIR/rheinc_core.a"
LEXER_ARCHIVE="$LIB_DIR/rheinc_lexer.a"
PP_ARCHIVE="$LIB_DIR/rheinc_preprocessor.a"
PARSER_ARCHIVE="$LIB_DIR/rheinc_parser.a"

DID_WORK=0
OBJECTS_REBUILT=0
ARCHIVES_REBUILT=0

sources=(
  src/source/RhSemantic/ConstExpr.cpp
  src/source/RhSemantic/Symbol.cpp
  src/source/RhSemantic/Scope.cpp
  src/source/RhSemantic/TypeContext.cpp
  src/source/RhSemantic/BuiltinRegistry.cpp
  src/source/RhSemantic/BuiltinRegistry/BuiltinRegistryFunctions.cpp
  src/source/RhSemantic/BuiltinRegistry/BuiltinRegistryTypes.cpp
  src/source/RhSemantic/SemanticContext.cpp
  src/source/RhSemantic/TypeResolver.cpp
  src/source/RhSemantic/SemanticAnalyzer.cpp
  src/source/RhSemantic/SemanticAnalyzer/SemanticAnalyzerTypes.cpp
  src/source/RhSemantic/SemanticDecl/SemanticDeclCollect.cpp
  src/source/RhSemantic/SemanticDecl/SemanticDeclAnalyze.cpp
  src/source/RhSemantic/SemanticStmt.cpp
  src/source/RhSemantic/SemanticExpr.cpp
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
[[ -f "$SEMANTIC_ARCHIVE" ]] && archive_hash="$(hash_file "$SEMANTIC_ARCHIVE")"
[[ -f "$SEMANTIC_ARCHIVE_STATE" ]] && stored_archive_hash="$(cat "$SEMANTIC_ARCHIVE_STATE")"
if [[ "$OBJECTS_REBUILT" == "0" && -n "$archive_hash" && "$archive_hash" == "$stored_archive_hash" ]] && "$AR" t "$SEMANTIC_ARCHIVE" >/dev/null 2>&1; then
  echo "[SKIP] archive fresh: $(basename "$SEMANTIC_ARCHIVE")"
else
  echo "[AR] $SEMANTIC_ARCHIVE"
  rm -f "$SEMANTIC_ARCHIVE"
  "$AR" rcs "$SEMANTIC_ARCHIVE" "${objects[@]}"
  hash_file "$SEMANTIC_ARCHIVE" > "$SEMANTIC_ARCHIVE_STATE"
  ARCHIVES_REBUILT=1
  DID_WORK=1
fi

if [[ "$BUILD_TESTS" == "1" ]]; then
  test_src_hash="$(hash_file "$TEST_SOURCE")"
  semantic_hash="$(hash_file "$SEMANTIC_ARCHIVE")"
  parser_hash="$(hash_file "$PARSER_ARCHIVE")"
  pp_hash="$(hash_file "$PP_ARCHIVE")"
  lexer_hash="$(hash_file "$LEXER_ARCHIVE")"
  core_hash="$(hash_file "$CORE_ARCHIVE")"
  bin_hash=""; sb=""; ss=""; sp=""; spp=""; sl=""; sc=""; sf=""
  [[ -f "$TEST_BINARY" ]] && bin_hash="$(hash_file "$TEST_BINARY")"
  [[ -f "$TEST_STATE" ]] && read -r sb ss sp spp sl sc sf < "$TEST_STATE" || true
  if [[ "$ARCHIVES_REBUILT" == "0" && -n "$bin_hash" && "$bin_hash" == "$sb" && "$test_src_hash" == "$ss" && "$semantic_hash" == "$sp" && "$parser_hash" == "$spp" && "$lexer_hash" == "$sl" && "$core_hash" == "$sc" && "$FLAGS_ID" == "$sf" ]]; then
    echo "[SKIP] test binary fresh: $(basename "$TEST_BINARY")"
  else
    tmp_binary="$BIN_TEST_DIR/.rheinc_semantic_test.$$"
    rm -f "$TEST_BINARY" "$tmp_binary"
    echo "[CXX-LD] $TEST_BINARY"
    "$CXX" $CXXFLAGS -I"$ROOT_DIR/src/include" "$TEST_SOURCE" "$SEMANTIC_ARCHIVE" "$PARSER_ARCHIVE" "$PP_ARCHIVE" "$LEXER_ARCHIVE" "$CORE_ARCHIVE" -o "$tmp_binary"
    mv -f "$tmp_binary" "$TEST_BINARY"
    bin_hash="$(hash_file "$TEST_BINARY")"
    printf '%s %s %s %s %s %s %s\n' "$bin_hash" "$test_src_hash" "$semantic_hash" "$parser_hash" "$pp_hash" "$lexer_hash" "$core_hash" "$FLAGS_ID" > "$TEST_STATE"
    DID_WORK=1
  fi

  if [[ "$RUN_TESTS" == "1" && "$DID_WORK" == "1" ]]; then
    tmp_run="$(mktemp /tmp/rheinc_semantic_test.XXXXXX)"
    cp "$TEST_BINARY" "$tmp_run"; chmod +x "$tmp_run"
    echo "[TEST] $tmp_run sintaks-awal/halo_dunia.rhc"
    (cd "$ROOT_DIR" && "$tmp_run" sintaks-awal/halo_dunia.rhc)
    rm -f "$tmp_run"
  fi
fi

if [[ "$DID_WORK" == "0" ]]; then
  echo "[OK] nothing to do"
else
  echo "[OK] RhSemantic build selesai"
  echo "  libs: $SEMANTIC_ARCHIVE"
  if [[ "$BUILD_TESTS" == "1" ]]; then echo "  bin-test: $TEST_BINARY"; fi
fi
