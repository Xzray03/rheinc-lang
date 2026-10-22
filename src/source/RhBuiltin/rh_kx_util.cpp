// rh_kx_util.cpp - pack/unpack pasangan double <-> bit-pattern.
#include "rh_kx.h"

KXView kx_pack(double re, double im) {
    KXView r;
    __builtin_memcpy(&r.re, &re, 8);
    __builtin_memcpy(&r.im, &im, 8);
    return r;
}
void kx_unpack(long long reb, long long imb, double& re, double& im) {
    __builtin_memcpy(&re, &reb, 8);
    __builtin_memcpy(&im, &imb, 8);
}
