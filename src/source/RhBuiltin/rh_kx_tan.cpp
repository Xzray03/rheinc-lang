#include "rh_kx.h"

extern "C" struct KXView rh_kx_tan(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    double s = kx_sin(re), c = kx_cos(re);
    double sh = kx_sinh(im), ch = kx_cosh(im);
    double den = c * c + sh * sh;
    return kx_pack(s * c / den, sh * ch / den);
}
