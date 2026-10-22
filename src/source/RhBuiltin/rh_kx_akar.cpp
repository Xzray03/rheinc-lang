#include "rh_kx.h"

extern "C" struct KXView rh_kx_akar(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    double r = kx_sqrt(kx_sqrt(re * re + im * im));
    double t = kx_atan2(im, re) / 2.0;
    return kx_pack(r * kx_cos(t), r * kx_sin(t));
}
