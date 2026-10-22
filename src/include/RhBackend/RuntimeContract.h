// RuntimeContract.h - SATU-SATUNYA sumber kebenaran nama simbol runtime.
//
// Dipakai bersama oleh RhBackend (codegen call rh_*) dan RhBuiltin
// (implementasi C++ runtime). Stub builtin di IR memakai nama sumber
// (mis. "tampilkan"); simbol object/runtime memakai prefix rh_*.
//
// ===========================================================================
// ATURAN STANDAR RUNTIME RhBuiltin (WAJIB dipatuhi semua file rh_*.cpp):
// ===========================================================================
// 1. SATU FILE, 1-2 FUNGSI extern "C". Util yang dipakai banyak file
//    (node helper, math, syscall, formatter) = file .cpp TERSENDIRI dengan
//    linkage eksternal BIASA. DILARANG static-inline di header (itu
//    MENDUPLIKASI kode ke tiap objek pemakai). Header (.h) hanya deklarasi
//    struct + prototipe (nol definisi fungsi).
// 2. DELEGASI, BUKAN DUPLIKASI: fungsi identik (antrian_tambah ==
//    tumpukan_tambah == daftar_tambah; hapus_depan == ambil head) memanggil
//    satu implementasi basis, bukan menyalin badan.
// 3. SELF-CONTAINED: linker TIDAK memakai -lm. Semua math tulis manual
//    (lihat rh_math_kx.cpp: sqrt Newton, sin/cos/atan Taylor, ln atanh).
//    Undefined reference saat link = pelanggaran aturan ini.
// 4. KONVENSI SLOT (stack agregat MENURUN, kata ke-0 = offset terendah):
//    state koleksi {head@[slot], count@[slot-8]}; larik inline elemen-i di
//    [base - i*esz]; hasil fat16 dua kata {kata0@[slot], kata1@[slot-8]}.
// 5. OPSI (Sum 16B) = SysVView {tag, payload}: ADA=0, KOSONG=1;
//    KOSONG = {ptr=0, packed=1}. Sum >16B (mis. Opsi(kompleks) 24B) TIDAK
//    bisa by-value - pakai VOID + DEST (contoh rh_kx_bagi_slot: tulis
//    {tag@0, re@-8, im@-16} langsung ke slot hasil).
// 6. RETURN STRING/TEKS = SysVView {ptr, len} dua kata rax:rdx.
// 7. ARGUMEN FLOAT ke runtime builtin: backend menolak jalur xmm generik.
//    Terima BIT-PATTERN long long via %rdi (contoh rh_float_to_text_kx)
//    atau sediakan handler khusus backend. Jangan andalkan retype tipe.
// 8. SETIAP simbol yang dipanggil codegen (literal string di backend)
//    atau via runtimeSymbolFor HARUS: (a) ada di pemetaan di bawah ini,
//    atau (b) terdefinisi di salah satu objek RhBuiltin. Nama simbol dan
//    handler backend yang mengecek string HARUS sinkron manual.
// 9. DAFTAR SIMBOL OTOMATIS: build.sh meng-glob *.cpp; file baru terkompilasi
//    & masuk manifest nm tanpa konfigurasi. Link PER-SIMBOL (closure nm):
//    objek hanya tertarik jika simbolnya dipakai program - util eksternal
//    yang tak terpakai TIDAK ikut ke binary.
// 10. BUFFER STATIK hati-hati: formatter menerima char* out dari pemanggil
//     (rh_fmt.cpp), bukan static internal per file.
// 11. SATU KESATUAN BOLEH >2 fungsi hanya jika berbagi state statik yang
//     tak terpisah (contoh sah: rh_mem.cpp TTD allocator; rh_math_kx.cpp
//     engine berantai). Justifikasi wajib ada di komentar kepala file.
// ===========================================================================

#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace rhc {

// Pemetaan nama builtin sumber -> simbol runtime (Fase 0: I/O dasar).
inline std::optional<std::string> runtimeSymbolFor(std::string_view builtinName) {
    if (builtinName == "tampilkan") return std::string{"rh_print"};
    if (builtinName == "baca")      return std::string{"rh_read"};
    if (builtinName == "tulis_file") return std::string{"rh_write_file"};
    if (builtinName == "baca_file")  return std::string{"rh_read_file"};
    if (builtinName == "hapus_file") return std::string{"rh_unlink"};
    if (builtinName == "gabung")     return std::string{"rh_concat_array_to"};
    if (builtinName == "urutkan")    return std::string{"rh_sort"};
    if (builtinName == "potong")     return std::string{"rh_slice_str"};
    if (builtinName == "ambil_ada")  return std::string{"rh_unwrap_ada"};
    if (builtinName == "ke_teks")    return std::string{"rh_int_to_text"};
    if (builtinName == "ke_bilangan") return std::string{"rh_str_to_int"};
    if (builtinName == "ke_desimal")  return std::string{"rh_str_to_decimal"};
    if (builtinName == "kx")          return std::string{"rh_kx_construct"};
    if (builtinName == "kx_modulus")  return std::string{"rh_kx_modulus"};
    if (builtinName == "kx_argumen")  return std::string{"rh_kx_argumen"};
    if (builtinName == "kx_konjugat") return std::string{"rh_kx_konjugat"};
    if (builtinName == "kx_polar")    return std::string{"rh_kx_polar"};
    if (builtinName == "kx_akar")     return std::string{"rh_kx_akar"};
    if (builtinName == "kx_pangkat")  return std::string{"rh_kx_pangkat"};
    if (builtinName == "kx_sin")      return std::string{"rh_kx_sin"};
    if (builtinName == "kx_cos")      return std::string{"rh_kx_cos"};
    if (builtinName == "kx_tan")      return std::string{"rh_kx_tan"};
    if (builtinName == "ke_kompleks") return std::string{"rh_ke_kompleks"};
    if (builtinName == "ke_teks_kompleks") return std::string{"rh_ke_teks_kompleks"};
    if (builtinName == "ke_teks_desimal")  return std::string{"rh_float_to_text_kx"};
    if (builtinName == "kx_tambah") return std::string{"rh_kx_tambah"};
    if (builtinName == "kx_kurang") return std::string{"rh_kx_kurang"};
    if (builtinName == "kx_kali")   return std::string{"rh_kx_kali"};
    if (builtinName == "kx_bagi")   return std::string{"rh_kx_bagi_slot"};
    if (builtinName == "ke_logika")   return std::string{"rh_str_to_bool"};
    if (builtinName == "ke_karakter") return std::string{"rh_str_to_char"};
    if (builtinName == "salin")       return std::string{"rh_memcpy"};
    if (builtinName == "iterator_mulai")     return std::string{"rh_iterator_mulai"};
    if (builtinName == "iterator_akhir")     return std::string{"rh_iterator_akhir"};
    if (builtinName == "iterator_selesai")   return std::string{"rh_iterator_selesai"};
    if (builtinName == "iterator_berikutnya") return std::string{"rh_iterator_berikutnya"};
    if (builtinName == "iterator_sebelumnya") return std::string{"rh_iterator_sebelumnya"};
    if (builtinName == "daftar")    return std::string{"rh_daftar"};
    if (builtinName == "daftar_tambah") return std::string{"rh_daftar_tambah"};
    if (builtinName == "antrian")   return std::string{"rh_antrian"};
    if (builtinName == "antrian_tambah") return std::string{"rh_antrian_tambah"};
    if (builtinName == "antrian_ambil")  return std::string{"rh_antrian_ambil"};
    if (builtinName == "tumpukan")  return std::string{"rh_tumpukan"};
    if (builtinName == "tumpukan_tambah") return std::string{"rh_tumpukan_tambah"};
    if (builtinName == "tumpukan_ambil")  return std::string{"rh_tumpukan_ambil"};
    if (builtinName == "tumpukan_ukuran")  return std::string{"rh_tumpukan_ukuran"};
    if (builtinName == "tumpukan_kosong")  return std::string{"rh_tumpukan_kosong"};
    if (builtinName == "tumpukan_bersihkan") return std::string{"rh_tumpukan_bersihkan"};
    if (builtinName == "tumpukan_atas")  return std::string{"rh_tumpukan_atas"};
    if (builtinName == "tumpukan_ke")  return std::string{"rh_tumpukan_ke"};
    if (builtinName == "tumpukan_cari")  return std::string{"rh_tumpukan_cari"};
    if (builtinName == "tumpukan_ke_larik") return std::string{"rh_tumpukan_ke_larik"};
    if (builtinName == "tumpukan_ke_larik_view") return std::string{"rh_tumpukan_ke_larik_view"};
    if (builtinName == "antrian_ukuran")   return std::string{"rh_tumpukan_ukuran"};
    if (builtinName == "antrian_kosong")   return std::string{"rh_tumpukan_kosong"};
    if (builtinName == "antrian_bersihkan") return std::string{"rh_tumpukan_bersihkan"};
    if (builtinName == "antrian_ke")       return std::string{"rh_tumpukan_ke"};
    if (builtinName == "antrian_cari")     return std::string{"rh_tumpukan_cari"};
    if (builtinName == "antrian_ke_larik") return std::string{"rh_tumpukan_ke_larik"};
    if (builtinName == "daftar_ukuran")   return std::string{"rh_tumpukan_ukuran"};
    if (builtinName == "daftar_kosong")   return std::string{"rh_tumpukan_kosong"};
    if (builtinName == "daftar_bersihkan") return std::string{"rh_tumpukan_bersihkan"};
    if (builtinName == "daftar_ke")       return std::string{"rh_tumpukan_ke"};
    if (builtinName == "daftar_cari")     return std::string{"rh_tumpukan_cari"};
    if (builtinName == "daftar_ke_larik") return std::string{"rh_tumpukan_ke_larik"};
    if (builtinName == "daftar_tambah_belakang") return std::string{"rh_daftar_tambah_belakang"};
    if (builtinName == "daftar_tambah_depan") return std::string{"rh_daftar_tambah"};
    if (builtinName == "daftar_hapus_depan") return std::string{"rh_daftar_hapus_depan"};
    if (builtinName == "daftar_hapus_belakang") return std::string{"rh_daftar_hapus_belakang"};
    if (builtinName == "daftar_dari_larik") return std::string{"rh_daftar_dari_larik"};
    if (builtinName == "tumpukan_dari_larik") return std::string{"rh_tumpukan_dari_larik"};
    if (builtinName == "panjang")   return std::string{"rh_len"};
    if (builtinName == "sistem")    return std::string{"rh_system"};
    if (builtinName == "pangkat")   return std::string{"rh_pow"};
    if (builtinName == "alokasikan") return std::string{"rh_alloc"};
    if (builtinName == "bebaskan")  return std::string{"rh_free"};
    if (builtinName == "realokasi") return std::string{"rh_realloc"};
    return std::nullopt;
}

}  // namespace rhc
