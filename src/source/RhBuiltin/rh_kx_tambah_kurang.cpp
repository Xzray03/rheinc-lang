#include "rh_kx.h"

extern "C" struct KXView rh_kx_tambah(long long ra, long long ia,
                                      long long rb, long long ib) {
    double ar, ai, br, bi;
    kx_unpack(ra, ia, ar, ai); kx_unpack(rb, ib, br, bi);
    return kx_pack(ar + br, ai + bi);
}
extern "C" struct KXView rh_kx_kurang(long long ra, long long ia,
                                      long long rb, long long ib) {
    double ar, ai, br, bi;
    kx_unpack(ra, ia, ar, ai); kx_unpack(rb, ib, br, bi);
    return kx_pack(ar - br, ai - bi);
}
