// rh_parse.cpp - parse teks (ptr,len) ke double (skip ws, +/-, int,
// frac). Dipakai rh_kx_construct & rh_ke_kompleks.
#include "rh_kx.h"

double kx_parse(const char* p, long len) {
    long i = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) ++i;
    bool neg = false;
    if (i < len && (p[i] == '-' || p[i] == '+')) { neg = (p[i] == '-'); ++i; }
    double v = 0.0;
    bool any = false;
    while (i < len && p[i] >= '0' && p[i] <= '9') { v = v * 10.0 + (p[i] - '0'); any = true; ++i; }
    if (i < len && p[i] == '.') {
        ++i;
        double f = 0.1;
        while (i < len && p[i] >= '0' && p[i] <= '9') { v += (p[i] - '0') * f; f *= 0.1; any = true; ++i; }
    }
    if (!any) return 0.0;
    return neg ? -v : v;
}
