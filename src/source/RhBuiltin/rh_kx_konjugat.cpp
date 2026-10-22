#include "rh_kx.h"

extern "C" struct KXView rh_kx_konjugat(long long reb, long long imb) {
    double re, im; kx_unpack(reb, imb, re, im);
    return kx_pack(re, -im);
}
