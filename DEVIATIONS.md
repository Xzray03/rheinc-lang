# DEVIATIONS.md — Penyimpangan Desain Rheinc

Dokumen ini mencatat penyimpangan disengaja dari spesifikasi bahasa/IR,
dirujuk dari kode dengan penanda `DEVIATIONS.md D-N`.

## D-1: Semantik `jamin` (defer) + floor division

**Jamin/defer.** Ekspansi `jamin` bersifat **FIFO per scope**:
saat scope keluar normal (`CompoundStmt` popEnv), body jamin scope
tersebut disisipkan di block saat ini SEBELUM popEnv. Saat `kembalikan`,
semua jamin aktif disisipkan **LIFO scope-wise** (scope dalam dulu),
FIFO per scope. Batasan sementara: `berhenti`/`lanjut` yang melompat
keluar scope **tidak** memicu jamin scope yang dilewati — perilaku
eksplisit belum didefinisikan di korpus. (`RhAST/ExpandDefer.cpp`)

**Floor division.** `DivFloor` (dan `Mod` yang menyertainya, `floorMod`)
memakai semantik floor division untuk tipe signed: `-7 DivFloor 2 = -4`,
`-7 Mod 2 = 1`. Backend mengoreksi hasil `idivq` bila sisa != 0 dan
tanda pembagi berbeda (decq untuk DivFloor; add divisor untuk Mod).
Konsisten juga di `PeepholeConstFold` (fold konstan) dan menjadi dasar
reduksi `x / 2^k -> Shr` pada `StrengthReduction` (arithmetic shift
ekuivalen floor-div untuk pangkat dua). `Div` tetap trunc division
(pembulatan ke nol) dan **tidak** direduksi ke shift untuk signed.
(`RhBackend/X86_64SysVEmitter.cpp`)

## D-2: TypeVariable terikat di level layout memakai default ABI

Tipe generik yang belum ter-substitusi penuh saat pemetaan tipe
(`TypeKind::TypeVariable` di `MapSemanticType.cpp`) dipetakan ke integer
signed 64-bit (`bilangan`) — nilai default ABI internal. Instantiasi
generik utuh (basis builtin/sum/object) menentukan layout konkret;
hanya slot yang benar-benar masih variabel yang memakai default ini.
Object type (`BuiltinObjectType`) dengan argumen generik dipetakan ke
layout yang sama dengan object tanpa argumen; member yang menyebut
parameter tipe ikut memakai default D-2.

## D-3: Konkurensi "sync-structural" (async/coroutine belum sesungguhnya)

Lowering konkurensi bersifat struktural-sinkron; suspensi sesungguhnya
(state machine korutin) adalah fase berikutnya:

- Modifier `async`/`asinkron`/`coroutine`/`generator` pada fungsi
  **diabaikan** saat lowering — fungsi diterjemahkan seperti fungsi
  biasa. Modifier lain ditolak (`RhAST/ValidateAst.cpp`).
- `jeda` / `jeda sampai kondisi`: kondisi tetap dievaluasi untuk side
  effects, tetapi tidak ada suspensi (`RhAST/LowerStmt.cpp`).
- `tunda` (await) diteruskan seperti pemanggilan biasa
  (`RhAST/LowerExpr.cpp`).

## D-4: Konstanta integer & desimal — ImmV 32-bit

`ImmV` hanya 32-bit, sehingga:

- `ConstInt` menyimpan nilai **inline** (bukan constant pool) sebagai
  1 operand `ImmV` untuk nilai <= 0xFFFFFFFF, atau 2 operand `ImmV`
  (lo, hi 32-bit) untuk nilai 64-bit penuh. Encoding kanonis lewat
  helper `encodeConstIntOperands`/`decodeConstIntValue` di `IrInstr.h`
  — SATU-SATUNYA jalur encode/decode agar producer (RhAST), optimizer,
  dan backend konsisten. Boolean konstan juga inline (`ConstBool`).
- Konstanta desimal (bit pattern double 64-bit) tidak muat di `ImmV`:
  `ConstantPropagation` menulis ulang operand `ValueV` konstanta desimal
  menjadi `ConstantV(constId)` yang merujuk `IrConstant::FloatBits` di
  constant pool (dedup tipe+bit pattern). `PeepholeConstFold` membaca
  kembali `ConstantV` untuk fold.

Konstanta lain di pool (representasi panjang-variabel): `String`,
`Bytes`, `BigInt`.

## D-5: Slice multi-dim — payload IR view vs tipe semantic Opsi

`m[s1, s2, ...]` (multi-dim): `SliceView` dibangun berurutan per
dimensi dan hasil akhir dibungkus `Opsi:ADA` sekali. Tipe payload IR
adalah view terakhir, sedangkan tipe semantic adalah
`Opsi<elementType>` — selisih ini dicatat di sini
(`RhAST/LowerExpr.cpp::lowerSliceMulti`).

## Catatan tambahan (tanpa penanda D di kode)

Float hanya `desimal` (double 64-bit, `BuiltinTypeKind::Desimal`).
Float 32-bit **tidak ada** dan **ditolak** oleh LayoutEngine/Verifier
(`"float sizeBytes harus 8"`). `ConstFloat` selalu 2 operand ImmV
(lo 32-bit, hi 32-bit dari bit pattern double). Eksponen/encoding lain
(f16, f80, ...) tidak direncanakan.
