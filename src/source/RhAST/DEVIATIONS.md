# Penyimpangan Implementasi RhAST terhadap Spesifikasi

Dokumen ini mencatat setiap penyimpangan implementasi dari
`RhAST_Spesifikasi.md` (desain terkunci v1.0), sesuai kewajiban:
"Setiap penyimpangan saat implementasi harus dicatat."

## D-1: Urutan ekspansi `jamin` (defer) adalah FIFO, bukan LIFO

- **Spesifikasi §4.4**: "setiap titik keluar scope menyisipkan urutan
  cleanup `defer` secara eksplisit (LIFO). IR tidak mengenali konstruk `defer`."
- **Keputusan**: FIFO — mengikuti corpus `sintaks-awal/defer.rhc` skenario 3
  ("Jamin FIFO"), yang menetapkan output `Isi prosedur, [jamin 1], [jamin 2], [jamin 3]`
  untuk tiga jamin yang didaftarkan berurutan 1, 2, 3.
- **Dampak**: `ExpandDefer.cpp` menyisipkan cleanup `JaminStmt` di setiap titik
  keluar scope dalam urutan pendaftaran (yang didaftarkan pertama jalan duluan).
- **Status**: korpus test Rheinc adalah sumber kebenaran perilaku bahasa;
  spesifikasi §4.4 perlu direvisi saat revisi dokumen berikutnya.
- **Tanggal kunci keputusan**: 2026-10-16.

## D-2: TypeVariable tak terikat -> bilangan 64-bit bertanda

- **Kondisi**: alias generik (mis. bentuk generik dari `bilangan<N>`)
  dipakai tanpa argumen; RhSemantic memberi `TypeVariableType` tanpa
  binding (tidak ada default/bounds di `TypeVariableType`).
- **Keputusan**: dipetakan ke `IrType::Int` 64-bit bertanda sebagai
  default ABI internal. Alasan: corpus `integer.rhc` memuat literal
  `9223372036854775807` (max i64) yang ditampung tipe `bilangan`.
- **Dampak**: `MapSemanticType.cpp` case `TypeKind::TypeVariable`.
- **Revisi dokumen**: spesifikasi belum mengatur default binding
  parameter generik; perlu dikunci saat revisi berikutnya.
- **Tanggal kunci keputusan**: 2026-10-16.
