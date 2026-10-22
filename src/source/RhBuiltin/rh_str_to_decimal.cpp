// rh_str_to_decimal.cpp - ke_desimal(s): parse teks (ptr,len) ke double.
// Whitespace skip, optional +/-, digit run, optional frac; invalid -> 0.0.
extern "C" double rh_str_to_decimal(const char* p, long len) {
    long i = 0;
    while (i < len && (p[i] == ' ' || p[i] == '\t')) ++i;
    bool neg = false;
    if (i < len && (p[i] == '-' || p[i] == '+')) {
        neg = (p[i] == '-');
        ++i;
    }
    double v = 0.0;
    bool any = false;
    while (i < len && p[i] >= '0' && p[i] <= '9') {
        v = v * 10.0 + (double)(p[i] - '0');
        any = true;
        ++i;
    }
    if (i < len && p[i] == '.') {
        ++i;
        double frac = 0.1;
        while (i < len && p[i] >= '0' && p[i] <= '9') {
            v += (double)(p[i] - '0') * frac;
            frac *= 0.1;
            any = true;
            ++i;
        }
    }
    if (!any) return 0.0;
    return neg ? -v : v;
}
