// rh_fprint.cpp - Runtime print desimal f64 (RhBuiltin).
//
// Format tetap 6 digit fraksi, trailing-zero dipangkas, tanda negatif
// benar (termasuk -0.0). Satu fungsi per file.

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

extern "C" void rh_fprint(double v) {
    char buf[40];
    int pos = 0;
    // tanda
    if (v < 0.0 || (v == 0.0 && (1.0 / v) < 0.0)) {
        buf[pos++] = '-';
        v = -v;
    }
    // bagian bulat
    const double scale = 1000000.0;  // 6 digit fraksi
    double scaled = v * scale;
    // deteksi overflow lembut: bila terlalu besar, cetak sebagai bulat saja
    if (scaled > 9.007199254740992e15) {
        char tmp[24];
        int tpos = 24;
        std::uint64_t whole = static_cast<std::uint64_t>(v);
        if (whole == 0) tmp[--tpos] = '0';
        while (whole > 0) {
            tmp[--tpos] = static_cast<char>('0' + (whole % 10));
            whole /= 10;
        }
        const int n = 24 - tpos;
        for (int i = 0; i < n; ++i) buf[pos++] = tmp[tpos + i];
        sysWrite(1, buf, static_cast<std::uint64_t>(pos));
        return;
    }
    std::uint64_t iscaled = static_cast<std::uint64_t>(scaled + 0.5);  // bulatkan
    std::uint64_t whole = iscaled / 1000000;
    std::uint64_t frac6 = iscaled % 1000000;
    // tulis whole
    {
        char tmp[24];
        int tpos = 24;
        if (whole == 0) tmp[--tpos] = '0';
        while (whole > 0) {
            tmp[--tpos] = static_cast<char>('0' + (whole % 10));
            whole /= 10;
        }
        const int n = 24 - tpos;
        for (int i = 0; i < n; ++i) buf[pos++] = tmp[tpos + i];
    }
    // fraksi: reconstruksi 6 kolom dari frac6, baru pangkas trailing zero.
    if (frac6 > 0) {
        char ftmp[6];
        std::uint64_t f = frac6;
        for (int d = 5; d >= 0; --d) {
            ftmp[d] = static_cast<char>('0' + (f % 10));
            f /= 10;
        }
        int flen = 6;
        while (flen > 0 && ftmp[flen - 1] == '0') --flen;
        if (flen > 0) {
            buf[pos++] = '.';
            for (int d = 0; d < flen; ++d) buf[pos++] = ftmp[d];
        }
    }
    sysWrite(1, buf, static_cast<std::uint64_t>(pos));
}
