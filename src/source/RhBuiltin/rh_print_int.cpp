// rh_print_int.cpp - Runtime print bilangan bulat (RhBuiltin).
//
// itoa + syscall write mentah (tanpa libc). Satu fungsi per file.

#include <cstdint>

namespace {

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

extern "C" void rh_print_int(std::int64_t v) {
    char buf[24];
    std::uint64_t n;
    bool neg = false;
    if (v < 0) {
        neg = true;
        // hati-hati INT64_MIN
        n = static_cast<std::uint64_t>(-(v + 1)) + 1;
    } else {
        n = static_cast<std::uint64_t>(v);
    }
    int pos = 24;
    if (n == 0) buf[--pos] = '0';
    while (n > 0) {
        buf[--pos] = static_cast<char>('0' + (n % 10));
        n /= 10;
    }
    if (neg) buf[--pos] = '-';
    sysWrite(1, buf + pos, static_cast<std::uint64_t>(24 - pos));
}
