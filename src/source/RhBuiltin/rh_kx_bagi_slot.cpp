#include "rh_kx.h"

// Pembagian -> Opsi(kompleks) 24B langsung ke slot hasil (3 kata menurun:
// tag@0, re@-8, im@-16); nol -> KOSONG(1).
extern "C" void rh_kx_bagi_slot(long long ra, long long ia,
                                long long rb, long long ib,
                                char* dest) {
    double ar, ai, br, bi;
    kx_unpack(ra, ia, ar, ai); kx_unpack(rb, ib, br, bi);
    double den = br * br + bi * bi;
    long long tag = 0;   // ADA
    double re = 0.0, im = 0.0;
    if (den == 0.0) {
        tag = 1;         // KOSONG
    } else {
        re = (ar * br + ai * bi) / den;
        im = (ai * br - ar * bi) / den;
    }
    *(long long*)dest = tag;
    __builtin_memcpy(dest - 8, &re, 8);
    __builtin_memcpy(dest - 16, &im, 8);
}
