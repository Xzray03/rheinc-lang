#include "rh_kx.h"

// ke_kompleks(teks) -> kompleks; parse "re + im i" (2 angka pertama).
extern "C" struct KXView rh_ke_kompleks(const char* p, long len) {
    long i = 0;
    double vals[2]; int vc = 0;
    while (i < len && vc < 2) {
        while (i < len && !((p[i] >= '0' && p[i] <= '9') || p[i] == '-' || p[i] == '+')) ++i;
        long j = i + ((i < len && (p[i] == '-' || p[i] == '+')) ? 1 : 0);
        while (j < len && ((p[j] >= '0' && p[j] <= '9') || p[j] == '.')) ++j;
        if (j > i) { vals[vc++] = kx_parse(p + i, j - i); i = j; } else ++i;
    }
    double re = vc > 0 ? vals[0] : 0.0;
    double im = vc > 1 ? vals[1] : 0.0;
    return kx_pack(re, im);
}
