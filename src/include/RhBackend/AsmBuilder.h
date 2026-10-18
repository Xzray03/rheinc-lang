// AsmBuilder.h - Penyusun teks assembly (AT&T, x86-64 ELF).
//
// Fase 0: API minimal untuk label, direktif, instruksi, dan komentar.
// Nanti diperluas (register naming, escape string, dst.) tanpa mengubah
// konsumen.

#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace rhc {

class AsmBuilder {
public:
    // --- label & simbol ---
    void label(const std::string& name);            // name:
    void globl(const std::string& name);            // .globl name
    void externSym(const std::string& name);        // .extern name (dedup)
    void typeFunction(const std::string& name);     // .type name,@function

    // --- section ---
    void section(const std::string& name);          // .section name
    void text();                                    // .text
    void rodata();                                  // .section .rodata
    void align(std::uint32_t bytes);                // .align n

    // --- data ---
    void asciiZ(const std::string& bytes);          // .asciz "..." (escaped)

    // --- instruksi (teks mentah, sudah diformat pemanggil) ---
    void gnuStackNote();       // .section .note.GNU-stack,"",@progbits

    void ins(const std::string& text);              // satu baris instruksi
    void comment(const std::string& text);          // # text

    // --- output ---
    std::string str() const { return out_.str(); }
    void clear() { out_.str(""); out_.clear(); }

private:
    std::ostringstream out_;
    std::vector<std::string> externs_;  // untuk dedup .extern
};

}  // namespace rhc
