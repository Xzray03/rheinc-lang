#include "rh_kx.h"

extern "C" double rh_kx_argumen(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    return kx_atan2(im, re);
}
