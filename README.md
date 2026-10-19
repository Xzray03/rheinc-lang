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
| `RhAST` | Lowering AST -> IR + **AstToIrTranslator** (bridge `releaseModule()`), ekspansi `jamin` (defer) |
| `RhIR` | Inti IR: module/function/instr/type/constant, **IrBuilder**, **IrDump**, **IrEditor** (mutasi index-preserving: replace/append/DCE/unreachable-block/insertInstrBefore), Verifier (Structural/Type/Layout/Final), LayoutEngine (System V AMD64), **IrManager + FinalIr**, ProviderManager (target ABI), **IrLinkVerifier** (verifikasi simbol antar modul + object dependency) |
| `RhOptimizer` | **OptimizerManager** (registry pass, verifyEachPass, fail-fast, fixpoint, epoch change-detection net) + pass: `const-prop`, `peephole-const-fold`, `static-var-unify`, `struct-layout` (DFE + field reordering satu pass; guard: idx dinamis / bitfield / semua-field-mati -> skip), `strength-reduce`, `copy-prop`, `cse` (global, dominator tree), `devirtualization` (CallIndirect -> Call langsung bila target fnptr resolve statis), `escape-analysis` (Alloc non-escaping: scalar promotion / dead local allocation; multi-store kompleks dilewati), `scalar-replacement` (multi-store non-escaping; aturan sound ketat: store terakhir dominan + pred bebas-store + anti-loop), `store-forwarding` (Load[p] -> value Store[p] untuk SSA-same pointer & GlobalV intra-fungsi; aturan dominasi ketat), `dead-store-elim` (store coalescing intra-block, anti-Call barrier; GlobalV skip karena race), `load-elimination` (redundant load cross-block dominasi; GlobalV skip), `simplify-cfg` (merge block linier via IrEditor::mergeBlocks), `licm` (natural loop + preheader; dua "bug" historis terbukti artefak binary stale — LICM aktif penuh; debug `RHEINC_DEBUG_LICM=1`), `loop-unroll` (full unrolling loop terhitung konstan; partial = peeling K=2 dengan phi exit `exit_peel`; runtime = route lewati), `dead-code-elim`, `dead-func-elim` (FuncV di-remap via IrEditor::removeDeadFunctions; External stub dijaga), `inline-functions` (callee single-block <=16 instruksi non-recursive, max 4 site), `unreachable-block-elim`; dominator analysis bersama di `RhIR/IrDomAnalysis.h` (`RhOptimizer/DomAnalysis.h` = alias kompatibilitas) |
| `RhBackend` | **Codegen x86_64-linux-sysv** (`X86_64SysVEmitter`, `GraphColoringRegAlloc`, `AsmBuilder`): FinalIr -> assembly tekstual `.s` -> object `.o` via `as`. Fase 0: object utama executable memakai CRT glue (`__rheinc_stub` + `__global_init`); object dependency tidak (anti duplikat simbol CRT) |
| `RhLinker` | Linker: object user + library runtime RhBuiltin -> executable/shared object via `ld`. Seleksi object runtime **"hanya yang dipakai"**: scan simbol undefined object user (nm), indeks manifest simbol->object dari direktori library, ambil closure transitif. Mode `-shared` tanpa CRT startup untuk Dynamic Library |
| `RhBuiltin` | Runtime bahasa: object `rh_*` (crt, io, math, mem, atoi, atof, print_int, fprint) di direktori library (`RHEINC_LIBRARY_DIR` saat build driver) |

Pipeline produksi (`rheinc`, di `src/source/RheincMain.cpp`):
lex -> preprocess -> parse -> semantic -> RhAST -> RhIR
(releaseModule -> adopt -> [optimizer] -> finalize -> freeze) ->
**RhBackend** (FinalIr -> .s -> .o, CRT glue bila executable utama) ->
**IrLinkVerifier** (simbol antar modul IR + dependency) ->
**RhLinker** (.o + runtime RhBuiltin -> executable / shared object).
Policy entry point diterapkan terpusat di RhMain: `.rhc` (Executable)
wajib tepat 1 entry (0 -> gagal; >1 -> gagal); `.rhh` (Declaration)
tepat 0 entry — final verify yang menuntut entry diampu policy ini,
hanya `MissingEntryFunction` murni yang diterima untuk modul declaration.
Driver sederhana saat ini; exit code: 0 semua file sukses, 1 ada file
gagal, 2 usage/gagal baca.

## Build & uji

```sh
export RHEINC_BUILD_DIR=/tmp/rheinc-build   # output build (source tak disalin)
bash build.sh --with-tests                  # semua modul + tool uji
Build/bin/rheinc <main.rhc> [-o <out.exe> | -oo <out.o> | -os <out.a> | -od <out.so> | -dump]
               [-opt] [--target <id>] [-Ilink <dir>]... [--link <file.rhc/.o/.a/.so> ...]
Build/bin-test/rheinc_ast_test [--full] [--no-loc] <file.rhc|.rhh>   # dump IR final
Build/bin-test/rheinc_lexer_test ...        # tool uji per modul lain:
Build/bin-test/rheinc_parser_test ...     #   parser, preprocessor, semantic
Build/bin-test/rheinc_preprocessor_test ...
Build/bin-test/rheinc_semantic_test ...
```

Opsi driver `rheinc`:

| Opsi | Arti |
|---|---|
| `<main.rhc>` | file utama; argumen paling pertama (`.rhh` = declaration) |
| `-o <out.exe>` | build **Executable** — entry di file utama tepat 1 |
| `-oo <out.o>` | build **Object** — tanpa entry; single file |
| `-os <out.a>` | build **Static Library** — tanpa entry; output wajib `.a`; `.so` dilarang sebagai input |
| `-od <out.so>` | build **Dynamic Library** — tanpa entry; output wajib `.so`; link via `ld -shared` |
| `-dump` | dump IR final ke stdout |
| `-opt` | jalankan optimizer (rantai 20 pass sampai konvergen) sebelum finalize; `-dump -opt` juga mencetak baris per-pass `; pass <nama> changed=.. n=..` |
| `--target <id>` | override target ABI (default `x86_64-linux-sysv`; target tak dikenal -> error) |
| `-Ilink <dir>` | auto-resolve simbol dari `.o`/`.a`/`.so` di direktori (rekursif); hanya untuk `-o`/`-od` |
| `--link <file..>` | dependency eksplisit sesuai target output; setelahnya hanya boleh file |

Korpus uji: `sintaks-awal/` (**81 file `.rhc`**). Baseline: **73 OK /
8 FAIL expected negative** (4 lexer_error + 4 rhpp error), identik mode
default dan `-opt`.

## Optimizer

Mode optimisasi (`-opt`): rantai
`const-prop -> peephole-const-fold -> static-var-unify -> struct-layout
-> strength-reduce -> copy-prop -> cse -> devirtualization ->
escape-analysis -> scalar-replacement -> store-forwarding ->
dead-store-elim -> load-elimination -> simplify-cfg -> licm ->
loop-unroll -> dead-code-elim -> dead-func-elim -> inline-functions ->
unreachable-block-elim` (**20 pass**), diulang sampai konvergen
(putaran maks sesuai konfigurasi OptimizerManager). Tanpa `-opt`:
pipeline IR murni. Semua pass index-preserving via IrEditor.

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

Backend IR + optimizer + **codegen** (`RhBackend`: emit `.s` lalu `.o`
via `as`) + **linker** (`RhLinker`: runtime RhBuiltin terpilih via
closure simbol -> `ld`) berfungsi **end-to-end**: program native
x86_64-linux-sysv dapat dikompilasi dan dijalankan. Driver `rheinc`
mendukung 4 target output (Executable/Object/Static Library/Dynamic
Library), `-Ilink` auto-resolve, dan `IrLinkVerifier` — untuk target
non-executable `UnresolvedExternal` non-fatal (resolve saat consumer
final melink). Invariant dominasi SSA ditegakkan penuh di structural
verify (seluruh korpus lolos di mode default dan `-opt`; smoke negatif:
use tak-terdominasi ditolak, dua kontrol positif diterima).
