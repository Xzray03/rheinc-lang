// rh_atof.cpp - Runtime konversi teks ke desimal f64 (RhBuiltin).
//
// Parse fixed-point (tanda '-', satu titik desimal). Satu fungsi per file.

#include <cstdint>

extern "C" double rh_atof(const char* s, std::int64_t len) {
    std::int64_t i = 0;
    bool neg = false;
    if (i < len && s[i] == '-') { neg = true; ++i; }
    double whole = 0.0;
    for (; i < len; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') break;
        whole = whole * 10.0 + static_cast<double>(c - '0');
    }
    double frac = 0.0;
    if (i < len && s[i] == '.') {
        ++i;
        double scale = 0.1;
        for (; i < len; ++i) {
            const char c = s[i];
            if (c < '0' || c > '9') break;
            frac += static_cast<double>(c - '0') * scale;
            scale *= 0.1;
        }
    }
    const double v = whole + frac;
    return neg ? -v : v;
}
