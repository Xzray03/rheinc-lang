# rheinc-lang
Mencoba membuat bahasa pemrograman berbasis Bahasa Indonesia >_<

## Ikhtisar modul

| Modul | Tanggung jawab |
|---|---|
| `RhCore` | SourceManager, DiagnosticEngine, LangOptions, **ModuleKind** (policy entry point: executable = tepat 1 entry, declaration = 0) |
| `RhLexer` | Tokenisasi + keyword Bahasa Indonesia |
| `RhPreprocessor` | Directive (`#tambahkan` dsb., include tekstual), HeaderSearch |
| `RhParser` | AST |
| `RhSemantic` | Analisis semantik + tipe (incl. BuiltinRegistry, object type) |
| `RhAST` | Lowering AST -> IR + **AstToIrTranslator** (bridge `releaseModule()`) |
| `RhIR` | Inti IR: module/function/instr/type/constant, **IrBuilder**, **IrDump**, **IrEditor** (mutasi index-preserving: replace/append/DCE/unreachable-block/insertInstrBefore), Verifier (Structural/Type/Layout/Final), LayoutEngine (System V AMD64), **IrManager + FinalIr**, ProviderManager (target ABI), **IrLinkVerifier** (verifikasi simbol antar modul) |
| `RhOptimizer` | **OptimizerManager** (registry pass, verifyEachPass, fail-fast, fixpoint, epoch change-detection net) + pass: `const-prop`, `peephole-const-fold`, `strength-reduce`, `cse` (global, dominator tree), `licm` (natural loop + preheader; dua bug historis — OOB `defBlock` & hoisting tanpa cek ketersediaan operand — sudah diperbaiki), `loop-unroll` (full unrolling untuk loop terhitung konstan berbentuk ketat; partial = loop peeling K=2 untuk loop berbentuk ketat dengan batas dinamis, lengkap dengan phi exit bernama `exit_peel`; runtime = route lewati), `dead-code-elim`, `unreachable-block-elim`; dominator analysis bersama di `RhIR/IrDomAnalysis.h` (`RhOptimizer/DomAnalysis.h` = alias kompatibilitas) |

Pipeline produksi (`rheinc`, sementara di `src/source/RheincMain.cpp`):
lex -> preprocess -> parse -> semantic -> RhAST -> RhIR
(releaseModule -> adopt -> [optimizer] -> finalize -> freeze) ->
link-time symbol verification. Policy entry point diterapkan terpusat
di RhMain untuk jalur produksi (`.rhh` declaration-only tidak
dijalankan, bebas entry; `.rhc` executable wajib tepat 1 entry).

## Build & uji

```sh
export RHEINC_BUILD_DIR=/tmp/rheinc-build   # output build (source tak disalin)
bash build.sh --with-tests                  # semua modul + tool uji
Build/bin/rheinc [--dump] [-opt] [--target <id>] [-o out] file...
Build/bin-test/rheinc_ast_test [--full] [--no-loc] file...   # dump IR final
```

Korpus uji: `sintaks-awal/` (85 file). Baseline: 76 OK / 10 FAIL
expected negative, identik mode default dan `-opt`.

## Optimizer

Mode optimisasi (`-opt`): rantai `const-prop -> peephole-const-fold ->
strength-reduce -> cse -> licm -> loop-unroll -> dead-code-elim ->
unreachable-block-elim` (8 pass), diulang sampai konvergen (maks 8 putaran).
Tanpa `-opt`: pipeline IR murni. Semua pass index-preserving via IrEditor
(debug LICM: `RHEINC_DEBUG_LICM=1`).
(lihat `DEVIATIONS.md` untuk penyimpangan desain D-1..D-5).

Structural verify juga menguji dominasi SSA (hardening, aktif penuh):
setiap operand `ValueV` — termasuk incoming phi — harus didefinisikan
instruksi yang mendominasi titik pakai. Incoming phi diuji dengan semantik
edge (value mendominasi block sumber pair `[BlockV, ValueV]`); cascade
phi same-block adalah konvensi translator yang sah. Audit yang
memungkinkan strict penuh: fix label cabang bersarang (ternary:
`thenEnd`/`elseEnd`), variabel elemen foreach bersifat scoped terhadap
loop, dan merge phi latch `%latch` untuk edge `lanjutkan`. Error:
`DefDoesNotDominateUse` dengan lokasi lengkap. Bypass diagnosis:
`RHEINC_SKIP_DOMCHECK=1`.

## Status

Backend IR + optimizer + driver produksi sederhana sudah berfungsi
end-to-end. Invariant dominasi SSA ditegakkan penuh di structural
verify (seluruh korpus 85 file lolos, termasuk jalur `-opt` dengan 7
pass; smoke negatif: use tak-terdominasi ditolak, dua kontrol positif
diterima). Codegen (emit `.s`/object) belum ada — keputusan arah
menunggu.
