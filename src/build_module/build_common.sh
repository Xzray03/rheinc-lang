#!/usr/bin/env bash
# Helper build bersama untuk modul Rheinc.
# Dipakai dengan `source` dari src/build_module/<Modul>/build.sh.
#
# Strategi incremental:
# - setiap compile C/C++ memakai -MMD -MP sehingga GCC menulis <object>.d
#   berisi daftar header user (.h/.hpp) yang benar-benar dipakai.
# - rebuild hanya jika: object/dep hilang, source/deps lebih baru dari
#   object, atau flags compiler berubah (ditangani script pemanggil).
# - archive di-skip hanya jika isi member sesuai state dan semua object
#   tidak lebih baru dari archive.

rh_hash_file() {
  [[ -f "$1" ]] || return 1
  cksum "$1" | awk '{print $1}'
}

rh_flags_id() {
  printf '%s' "$*" | cksum | awk '{print $1}'
}

# Ambil prerequisite rule pertama dari Makefile-style dep file (.d).
# Phony target -MP yang berada setelah rule pertama sengaja diabaikan.
rh_first_rule_prereqs() {
  local dep_file="$1"
  awk '
    NR==1 {
      sub(/^[^:]*:/, "")
      hold = $0
      if (hold !~ /\\$/) { print hold; exit }
      next
    }
    {
      line = $0
      cont = (line ~ /\\$/)
      sub(/\\$/, "", line)
      hold = hold " " line
      if (!cont) { print hold; exit }
    }
  ' "$dep_file"
}

# return 0 jika object perlu dibangun ulang.
rh_needs_rebuild() {
  local object_file="$1"
  local dep_file="$2"
  local source_file="$3"

  [[ -f "$object_file" ]] || return 0
  [[ -f "$dep_file" ]] || return 0
  [[ -f "$source_file" ]] || return 0
  [[ "$source_file" -nt "$object_file" ]] && return 0

  local deps_text=""
  deps_text="$(rh_first_rule_prereqs "$dep_file")" || return 0

  local -a deps=()
  # Path project Rheinc tidak memakai spasi; parser tetap toleran token kosong.
  read -r -a deps <<< "$deps_text"
  local dep=""
  for dep in "${deps[@]}"; do
    [[ -z "$dep" || "$dep" == "\\" ]] && continue
    [[ ! -e "$dep" ]] && return 0
    [[ "$dep" -nt "$object_file" ]] && return 0
  done
  return 1
}

rh_compile_cxx() {
  local source_file="$1"
  local object_file="$2"
  shift 2
  mkdir -p "$(dirname "$object_file")"
  rm -f "$object_file" "$object_file.d"
  "$CXX" $CXXFLAGS -MMD -MP -MF "$object_file.d" "$@" \
    -c "$source_file" -o "$object_file"
}

# return 0 jika archive masih fresh.
rh_archive_fresh() {
  local archive_file="$1"
  local state_file="$2"
  shift 2
  local objects=("$@")

  [[ -f "$archive_file" ]] || return 1
  [[ -f "$state_file" ]] || return 1

  local expected actual
  expected="$(printf '%s\n' "${objects[@]##*/}")"
  actual="$("$AR" t "$archive_file" 2>/dev/null)" || return 1
  [[ "$expected" == "$actual" ]] || return 1

  local obj=""
  for obj in "${objects[@]}"; do
    [[ "$obj" -nt "$archive_file" ]] && return 1
  done
  return 0
}

rh_update_archive() {
  local archive_file="$1"
  local state_file="$2"
  shift 2
  local objects=("$@")
  mkdir -p "$(dirname "$archive_file")" "$(dirname "$state_file")"
  echo "[AR] $archive_file"
  rm -f "$archive_file"
  "$AR" rcs "$archive_file" "${objects[@]}"
  "$AR" t "$archive_file" > "$state_file"
}
