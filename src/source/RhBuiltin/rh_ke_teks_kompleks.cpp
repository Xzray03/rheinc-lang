#include "rh_kx.h"

// ke_teks kompleks -> "(re + im i)" (format 2 digit; im negatif: "-").
extern "C" struct SysVView rh_ke_teks_kompleks(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    static char buf[96];
    long n = 0;
    buf[n++] = '(';
    n += rh_fmt_double(re, 2, buf + n);
    buf[n++] = ' '; buf[n++] = '+'; buf[n++] = ' ';
    n += rh_fmt_double(im, 2, buf + n);
    buf[n++] = 'i'; buf[n++] = ')'; buf[n] = 0;
    SysVView r = { buf, n };
    return r;
}
