#include "rh_kx.h"

extern "C" struct KXView rh_kx_polar(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    return kx_pack(kx_sqrt(re * re + im * im), kx_atan2(im, re));
}
