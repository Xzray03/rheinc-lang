#include "rh_kx.h"

// ke_teks desimal (konteks kompleks) -> "%.6f" (bit-pattern long long
// via %rdi; arg-prep float ke runtime builtin belum didukung).
extern "C" struct SysVView rh_float_to_text_kx(long long vbits) {
    double v;
    __builtin_memcpy(&v, &vbits, 8);
    static char buf[40];
    long n = rh_fmt_double(v, 6, buf);
    SysVView r = { buf, n };
    return r;
}
