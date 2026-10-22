#include "rh_kx.h"

extern "C" struct KXView rh_kx_pangkat(long long reb, long long imb, long n) {
    double re, im; kx_unpack(reb, imb, re, im);
    double r = kx_sqrt(re * re + im * im);
    double t = kx_atan2(im, re);
    double rn = kx_pow(r, (double)n);
    double tn = t * (double)n;
    return kx_pack(rn * kx_cos(tn), rn * kx_sin(tn));
}
