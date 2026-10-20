// RuntimeContract.h - SATU-SATUNYA sumber kebenaran nama simbol runtime.
//
// Dipakai bersama oleh RhBackend (codegen call rh_*) dan RhBuiltin
// (implementasi C++ runtime). Stub builtin di IR memakai nama sumber
// (mis. "tampilkan"); simbol object/runtime memakai prefix rh_*.

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
    if (builtinName == "ke_logika")   return std::string{"rh_str_to_bool"};
    if (builtinName == "ke_karakter") return std::string{"rh_str_to_char"};
    if (builtinName == "salin")       return std::string{"rh_memcpy"};
    if (builtinName == "panjang")   return std::string{"rh_len"};
    if (builtinName == "sistem")    return std::string{"rh_system"};
    if (builtinName == "pangkat")   return std::string{"rh_pow"};
    if (builtinName == "alokasikan") return std::string{"rh_alloc"};
    if (builtinName == "bebaskan")  return std::string{"rh_free"};
    if (builtinName == "realokasi") return std::string{"rh_realloc"};
    return std::nullopt;
}

}  // namespace rhc
