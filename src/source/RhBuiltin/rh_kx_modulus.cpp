#include "rh_kx.h"

extern "C" double rh_kx_modulus(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    return kx_sqrt(re * re + im * im);
}
