// rh_io.cpp - Runtime I/O dasar Rheinc (RhBuiltin).
//
// Kontrak simbol: RuntimeContract.h (RhBackend). Fungsi dipanggil dari
// kode hasil RhBackend; argumen mengikuti ABI System V AMD64.
// Implementasi sengaja syscall-mentah (tanpa libc) agar executable
// bisa di-link langsung dengan ld + runtime RhBuiltin saja.

#include <cstdint>

namespace {

// syscall write: rax=1, rdi=fd, rsi=buf, rdx=len.
long sysWrite(int fd, const void* buf, std::uint64_t len) {
    long ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(1L), "D"(static_cast<long>(fd)), "S"(buf), "d"(len)
        : "rcx", "r11", "memory");
    return ret;
}

}  // namespace

extern "C" void rh_print(const char* s, std::uint64_t len) {
    sysWrite(1, s, len);
}
