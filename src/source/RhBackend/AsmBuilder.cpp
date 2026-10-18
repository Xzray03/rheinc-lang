// AsmBuilder.cpp - Implementasi penyusun teks assembly.

#include "RhBackend/AsmBuilder.h"

#include <algorithm>

namespace rhc {

namespace {
// Escape isi string untuk direktif .asciz (gaya GNU as).
std::string escapeAsciz(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (const unsigned char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        default:
            if (c < 0x20 || c >= 0x7F) {
                const char* hex = "0123456789abcdef";
                out += "\\x";
                out += hex[c >> 4];
                out += hex[c & 0xF];
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    return out;
}
}  // namespace

void AsmBuilder::label(const std::string& name) { out_ << name << ":\n"; }
void AsmBuilder::globl(const std::string& name) { out_ << "  .globl " << name << "\n"; }
void AsmBuilder::externSym(const std::string& name) {
    if (std::find(externs_.begin(), externs_.end(), name) == externs_.end()) {
        externs_.push_back(name);
        out_ << "  .extern " << name << "\n";
    }
}
void AsmBuilder::typeFunction(const std::string& name) {
    out_ << "  .type " << name << ",@function\n";
}
void AsmBuilder::section(const std::string& name) { out_ << "  .section " << name << "\n"; }
void AsmBuilder::text() { out_ << "  .text\n"; }
void AsmBuilder::rodata() { section(".rodata"); }
void AsmBuilder::align(std::uint32_t bytes) { out_ << "  .align " << bytes << "\n"; }
void AsmBuilder::asciiZ(const std::string& bytes) {
    out_ << "  .asciz \"" << escapeAsciz(bytes) << "\"\n";
}
void AsmBuilder::globl_label(const std::string& name) { out_ << "  .globl " << name << "\n"; }
void AsmBuilder::zero_bytes(std::uint32_t n) { out_ << "  .zero " << n << "\n"; }
void AsmBuilder::quad(std::uint64_t v) { out_ << "  .quad " << v << "\n"; }
void AsmBuilder::gnuStackNote() {
    out_ << "  .section .note.GNU-stack,\"\",@progbits\n";
}
void AsmBuilder::ins(const std::string& text) { out_ << "  " << text << "\n"; }
void AsmBuilder::comment(const std::string& text) { out_ << "  # " << text << "\n"; }

}  // namespace rhc
