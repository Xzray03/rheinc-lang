#!/usr/bin/env bash
# build_flags.sh - Pusat default konfigurasi build Rheinc.
#
# Dibaca dengan `source` oleh build.sh root dan seluruh script modul
# (src/build_module/<Modul>/build.sh) SEBELUM logika build apa pun.
# Requirement: ROOT_DIR sudah diset oleh pemanggil.
#
# Prioritas tiap variabel:
#   RHEINC_X (sudah diset caller/script induk) > X (env asli user) > default.
#
# Env asli pengguna (CXX, CXXFLAGS, AR, ...) TIDAK diubah. Hasil akhir
# diterbitkan sebagai variabel RHEINC_* yang di-export, sehingga nilai
# menyebar ke script anak (dependency antar modul) tanpa kehilangan
# konfigurasi dan tanpa menimpa environment asli.

if [[ -z "${ROOT_DIR:-}" ]]; then
  echo "build_flags.sh: ROOT_DIR belum diset oleh pemanggil" >&2
  return 1 2>/dev/null || exit 1
fi

: "${RHEINC_CXX:=${CXX:-g++}}"
: "${RHEINC_AR:=${AR:-ar}}"
: "${RHEINC_CXXFLAGS:=${CXXFLAGS:--std=c++20 -Wall -Wextra -Wpedantic -Werror}}"
: "${RHEINC_BUILD_DIR:=$ROOT_DIR/Build}"
: "${RHEINC_BUILD_TESTS:=1}"
: "${RHEINC_RUN_TESTS:=0}"

export RHEINC_CXX RHEINC_AR RHEINC_CXXFLAGS RHEINC_BUILD_DIR RHEINC_BUILD_TESTS RHEINC_RUN_TESTS
