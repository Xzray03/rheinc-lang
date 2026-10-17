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
| `RhOptimizer` | **OptimizerManager** (registry pass, verifyEachPass, fail-fast, fixpoint, epoch change-detection net) + pass: `const-prop`, `peephole-const-fold`, `strength-reduce`, `cse` (global, dominator tree), `licm` (natural loop + preheader; dua bug historis — OOB `defBlock` & hoisting tanpa cek ketersediaan operand — sudah diperbaiki), `copy-prop` (propagasi instruksi identitas: Add/Sub/Or/Xor/Shl/Shr(v,0), Mul(v,1), And(v,mask penuh) -> operand asli, sound via SSA), `devirtualization` (CallIndirect -> Call langsung bila target fnptr resolve statis: AddrOf @fn langsung atau Phi monomorfik semua-incoming-sama; polimorfik dilewati -- bahasa tanpa vtable, pemanggilan tidak langsung berasal dari first-class function/lambda), `escape-analysis` (Alloc non-escaping: escape konservatif via use-graph; scalar promotion -- satu Store dominan -> Load jadi stored value; dead local allocation -- tanpa Load -> Alloc+Store dieliminasi via IrEditor::eliminateInstrs; multi-store kompleks dilewati), `scalar-replacement` (multi-store non-escaping: Load diganti value store penampung dengan aturan sound ketat -- store terakhir dominan + pred bebas-store + anti-loop; alloc+store tereliminasi setelah semua Load terpromosi), `static-var-unify` (SVU kategori 1: global write-once -- store tepat 1x di .__global_init + init konstan -> semua Load jadi konstanta inline "truth" (dua global bernilai sama otomatis menyatu), instruksi dihapus, slot global dipadatkan via IrEditor::removeDeadGlobals), `struct-layout` (DFE + field reordering satu pass: field Struct tanpa Extract/Insert = mati (kecuali bila semua mati -- struct 0 field ditolak LayoutEngine), sisa diurutkan align/size desc, idx Extract/Insert di-remap + Construct positional di-remap + offset System V dihitung ulang; guard: idx dinamis / bitfield / semua-field-mati -> skip), `store-forwarding` (Load[p] -> value Store[p] untuk SSA-same pointer arbitrer (param/self, tanpa syarat non-escape) dan GlobalV intra-fungsi; aturan dominasi ketat seperti scalar-replacement: store penampung dominan + pred bebas-store-lain + anti-loop), `dead-store-elim` (bentuk store coalescing untuk IR whole-value: Store SSA-pointer non-escape yang ditimpa store berikutnya sebelum terbaca -> dihapus; intra-block, anti-Load-di-antara, anti-Call barrier; GlobalV skip karena race), `load-elimination` (redundant load cross-block dominasi: v2=Load[p] -> v1=Load[p] dengan dom(L1,L2) dan tak ada Store[p] yang reachable setelah L1 sebelum L2; SSA-same non-escape; GlobalV skip karena race), `simplify-cfg` (merge block linier single-succ/single-pred via IrEditor::mergeBlocks, iterasi sampai stabil, orphan disapu UBE hanya bila ada merge), `dead-func-elim` (fungsi non-Entry/GlobalInit/External yang tak jadi operand FuncV atau symbol mapping -> dihapus, FuncV di-remap via IrEditor::removeDeadFunctions; External stub dijaga = antarmuka runtime), `inline-functions` (inline expansion: Call ke callee Normal single-block <=16 instruksi non-recursive max 4 site -> body diklon dengan param->arg binding + SSA remap, use hasil Call dialihkan ke clone nilai Ret), `loop-unroll` (full unrolling untuk loop terhitung konstan berbentuk ketat; partial = loop peeling K=2 untuk loop berbentuk ketat dengan batas dinamis, lengkap dengan phi exit bernama `exit_peel`; runtime = route lewati), `dead-code-elim`, `unreachable-block-elim`; dominator analysis bersama di `RhIR/IrDomAnalysis.h` (`RhOptimizer/DomAnalysis.h` = alias kompatibilitas) |

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
peephole-const-fold -> static-var-unify -> strength-reduce ->
copy-prop -> cse -> devirtualization -> escape-analysis ->
scalar-replacement -> store-forwarding -> dead-store-elim ->
load-elimination -> simplify-cfg -> dead-func-elim -> inline-functions ->
struct-layout -> licm -> loop-unroll -> dead-code-elim ->
unreachable-block-elim` (20 pass), diulang sampai konvergen (maks 8
putaran).
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
