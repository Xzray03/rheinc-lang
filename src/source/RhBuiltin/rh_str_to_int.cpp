// rh_str_to_int.cpp - ke_bilangan(s): parse teks (ptr,len) ke long long.
// Whitespace skip, optional +/-, digit run; invalid -> 0.
extern "C" long long rh_str_to_int(const char* p, long len) {
    long i = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) ++i;
    bool neg = false;
    if (i < len && (p[i] == '-' || p[i] == '+')) {
        neg = (p[i] == '-');
        ++i;
    }
    long long v = 0;
    bool any = false;
    while (i < len && p[i] >= '0' && p[i] <= '9') {
        v = v * 10 + (p[i] - '0');
        any = true;
        ++i;
    }
    if (!any) return 0;
    return neg ? -v : v;
}
