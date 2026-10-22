// rh_fmt.cpp - format double manual "%.<digits>f" (tanpa libm).
// Dipakai rh_ke_teks_kompleks (2 digit) & rh_float_to_text_kx (6 digit).
#include "rh_kx.h"

int rh_fmt_double(double v, int digits, char* out) {
    bool neg = v < 0.0;
    if (neg) v = -v;
    long long ip = (long long)v;
    double fr = v - (double)ip;
    long long scale = 1;
    for (int k = 0; k < digits; ++k) scale *= 10;
    long long fp = (long long)(fr * (double)scale + 0.5);
    if (fp >= scale) { ip += 1; fp -= scale; }
    long n = 0;
    if (neg) out[n++] = '-';
    char tmp[24]; int ti = 0;
    if (ip == 0) tmp[ti++] = '0';
    while (ip > 0) { tmp[ti++] = (char)('0' + ip % 10); ip /= 10; }
    while (ti > 0) out[n++] = tmp[--ti];
    out[n++] = '.';
    for (long long d = scale / 10; d >= 1; d /= 10)
        out[n++] = (char)('0' + (fp / d) % 10);
    out[n] = 0;
    return (int)n;
}
