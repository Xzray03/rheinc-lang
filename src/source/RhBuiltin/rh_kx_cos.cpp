#include "rh_kx.h"

extern "C" struct KXView rh_kx_cos(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    return kx_pack(kx_cos(re) * kx_cosh(im), -kx_sin(re) * kx_sinh(im));
}
