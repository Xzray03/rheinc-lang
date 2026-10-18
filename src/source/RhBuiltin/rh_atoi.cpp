// rh_atoi.cpp - Runtime konversi teks ke bilangan bulat (RhBuiltin).
//
// Parse integer dari string (tanda '-' didukung). Satu fungsi per file.

#include <cstdint>

extern "C" std::int64_t rh_atoi(const char* s, std::int64_t len) {
    std::int64_t i = 0;
    bool neg = false;
    if (i < len && s[i] == '-') { neg = true; ++i; }
    std::int64_t v = 0;
    for (; i < len; ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') break;
        v = v * 10 + (c - '0');
    }
    return neg ? -v : v;
}
