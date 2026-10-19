#!/usr/bin/env bash
# TEST_AWAL.sh - Kompilasi, jalankan, dan validasi 30 file korpus RUNTIME OK.
#
# Usage:
#   ./TEST_AWAL.sh            compile + run + tampilkan semua output + validasi
#                           (diff stdout vs expected/) -> PASS/FAIL per file
#   ./TEST_AWAL.sh --update   regenerasi expected/ dari output aktual
#   ./TEST_AWAL.sh --quiet    validasi ringkas (hanya PASS/FAIL + ringkasan)
#
# Binary compiler: env RHEINC_BIN (default /tmp/rheinc-build/bin/rheinc)
set -u

ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/sintaks-awal"
EXP="$ROOT/expected"
BIN="${RHEINC_BIN:-/tmp/rheinc-build/bin/rheinc}"
WORK="$(mktemp -d /tmp/rheinc-test.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Normalisasi output non-deterministik (alamat memori hampa.rhc) agar
# diff vs expected stabil antar run.
normalize() {
    sed -E -e 's/(Buffer dialokasikan di: )[0-9]+/\1<ALAMAT>/' \
           -e 's|/tmp/rheinc-test\.[A-Za-z0-9]+|<CWD>|g' "$1"
}

FILES=(
  halo_dunia.rhc tes_dasar.rhc karakter.rhc teks.rhc integer.rhc
  float.rhc logika.rhc konstanta.rhc hampa.rhc increment-decrement.rhc
  bitwise.rhc urutan_operasi.rhc percabangan.rhc percabangan_pilih.rhc
  pilih.rhc fungsi_dan_prosedur.rhc bilangan_prima.rhc fibonacci.rhc
  perulangan.rhc rekursif.rhc prototipe_fungsi.rhc berhenti_lanjut.rhc
  scope_dan_shadowing.rhc interaktif.rhc perintah_sistem.rhc io.rhc
  ternary.rhc goto.rhc multi_goto.rhc array.rhc array_multi.rhc
  penggabungan_array.rhc
  rhpp/tambahkan.rhc rhpp/warning.rhc rhpp/error_warning_inactive.rhc
  rhpp/preprocessor.rhc rhpp/jika_makro_fungsi.rhc rhpp/variadic.rhc
  rhpp/variadic_opt.rhc rhpp/makro_fungsi_bersarang.rhc
)

MODE="${1:-}"
QUIET=0
[ "$MODE" = "--quiet" ] && QUIET=1

if [ ! -x "$BIN" ]; then
    echo "ERROR: compiler tidak ditemukan: $BIN" >&2
    echo "       set RHEINC_BIN atau rebuild (RHEINC_BUILD_DIR=/tmp/rheinc-build bash build.sh)" >&2
    exit 2
fi

pass=0; fail=0
failed=()

for f in "${FILES[@]}"; do
    name="${f%.rhc}"; name="${name//\//_}"
    out="$WORK/$name.out"
    err="$WORK/$name.err"
    exe="$WORK/$name.bin"

    # compile (artefak .s/.o backend tertulis di direktori sumber -> dibersihkan)
    "$BIN" "$SRC/$f" -o "$exe" >"$err" 2>&1
    rc=$?
    rm -f "${SRC}/${f%.rhc}.s" "${SRC}/${f%.rhc}.o"
    if [ $rc -ne 0 ]; then
        # retry sekali utk error transient (ld/as)
        "$BIN" "$SRC/$f" -o "$exe" >"$err" 2>&1
        rc=$?
        rm -f "${SRC}/${f%.rhc}.s" "${SRC}/${f%.rhc}.o"
    fi
    if [ $rc -ne 0 ]; then
        fail=$((fail+1)); failed+=("$f (compile rc=$rc)")
        continue
    fi

    # Run dari $WORK: cwd stabil utk `pwd`, dan copy rheinc executable
    # ke sana supaya ./rheinc (perintah_sistem.rhc) benar2 jalan —
    # portal /mnt menolak exec bit, /tmp tidak.
    cp "$BIN" "$WORK/rheinc"
    chmod +x "$WORK/rheinc"
    if [ -f "$ROOT/inputs/$name.in" ]; then
        (cd "$WORK" && timeout 10 "./$name.bin" <"$ROOT/inputs/$name.in" >"$out" 2>>"$err")
    else
        (cd "$WORK" && timeout 10 "./$name.bin" >"$out" 2>>"$err")
    fi
    rc=$?
    rm -f "$WORK/rheinc"

    if [ "$MODE" = "--update" ]; then
        mkdir -p "$EXP"
        normalize "$out" > "$EXP/$name.out"
        continue
    fi

    if [ $rc -ne 0 ]; then
        fail=$((fail+1)); failed+=("$f (run rc=$rc)")
        continue
    fi
    if [ ! -f "$EXP/$name.out" ]; then
        fail=$((fail+1)); failed+=("$f (expected/$name.out missing)")
        continue
    fi
    normalize "$out" > "$out.norm"
    if diff -q "$EXP/$name.out" "$out.norm" >/dev/null 2>&1; then
        pass=$((pass+1))
        status="PASS"
    else
        fail=$((fail+1)); failed+=("$f (output mismatch)")
        status="FAIL"
    fi

    if [ $QUIET -eq 0 ]; then
        echo "=== $f === [$status]"
        cat "$out"
        if [ -s "$err" ]; then
            echo "--- stderr ---"
            cat "$err"
        fi
        echo
    fi
done

if [ "$MODE" = "--update" ]; then
    echo "expected/ diregenerasi (${#FILES[@]} file) di $EXP"
    exit 0
fi

echo "=========================================="
echo "HASIL: $pass PASS / $fail FAIL (dari ${#FILES[@]} file)"
if [ $fail -gt 0 ]; then
    echo "FAIL LIST:"
    printf '  - %s\n' "${failed[@]}"
    exit 1
fi
echo "SEMUA PASS"
exit 0
