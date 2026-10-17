# DEVIATIONS.md — Penyimpangan Desain Rheinc

Dokumen ini mencatat penyimpangan disengaja dari spesifikasi bahasa/IR,
dirujuk dari kode dengan penanda `DEVIATIONS.md D-N`.

## D-1: DivFloor = floor division (pembulatan ke -inf)

`DivFloor` (dan `Mod` yang menyertainya, `floorMod`) memakai semantik
floor division untuk tipe signed: `-7 DivFloor 2 = -4`, `-7 Mod 2 = 1`.
Ini dipakai konsisten di `PeepholeConstFold` (fold konstan) dan menjadi
dasar reduksi `x / 2^k -> Shr` pada `StrengthReduction` (arithmetic
shift ekuivalen floor-div untuk pangkat dua). `Div` tetap trunc division
(pembulatan ke nol) dan **tidak** direduksi ke shift untuk signed.

## D-2: TypeVariable terikat di level layout memakai default ABI

Tipe generik yang belum ter-substitusi penuh saat pemetaan tipe
(`TypeKind::TypeVariable` di `MapSemanticType.cpp`) dipetakan ke integer
signed 64-bit (`bilangan`) — nilai default ABI internal. Instantiasi
generik utuh (basis builtin/sum/object) menentukan layout konkret;
hanya slot yang benar-benar masih variabel yang memakai default ini.
Object type (`BuiltinObjectType`) dengan argumen generik dipetakan ke
`IrTypeKind::Struct` yang sama dengan object tanpa argumen; member yang
menyebut parameter tipe ikut memakai default D-2.

## D-3: Float hanya double 64-bit (desimal)

Bahasa hanya punya `desimal` (double 64-bit, `BuiltinTypeKind::Desimal`).
Float 32-bit **tidak ada** dan **ditolak** oleh LayoutEngine/Verifier
(`"float sizeBytes harus 8"`). `ConstFloat` selalu 2 operand ImmV
(lo 32-bit, hi 32-bit dari bit pattern double). Eksponen/encoding lain
(f16, f80, ...) tidak direncanakan.

## D-4: Konstanta integer & boolean inline, bukan di constant pool

`ConstInt`/`ConstBool` menyimpan nilai sebagai operand `ImmV` inline
(integer biasa TIDAK masuk constant pool — sesuai komentar `IrInstr.h`).
Hanya konstanta besar yang butuh representasi panjang-variable yang
masuk pool: `FloatBits` (desimal, hasil propagasi — lihat
`ConstantPropagation`), `String`, `Bytes`, `BigInt`. Karena `ImmV`
32-bit, propagasi konstanta desimal memakai jalur khusus `ConstantV`
(D-5).

## D-5: Propagasi konstanta desimal via ConstantV

`ConstantPropagation` menulis ulang operand `ValueV` konstanta desimal
menjadi `ConstantV(constId)` yang merujuk `IrConstant::FloatBits` di
constant pool (dedup tipe+bit pattern). `ImmV` hanya 32-bit sehingga
tidak bisa membawa bit pattern double; `ConstantV` tidak dibatasi
demikian. `PeepholeConstFold` membaca kembali `ConstantV` untuk fold.
