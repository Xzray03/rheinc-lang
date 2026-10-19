// rh_read.cpp - Runtime: baca satu line dari stdin, parse per tipe,
// tulis hasil ke slot variabel. Pola syscall langsung tanpa libc.
//
// typeId: 0=int(signed) 1=uint 2=karakter 3=logika 4=desimal 5=teks
// teks: disalin ke buffer statik (satu baca-teks aktif per program).
extern "C" void rh_read(unsigned long long* slot, int typeId) {
    static char buf[4096];
    static char strbuf[4096];
    int n = 0;
    for (;;) {
        char c;
        long r;
        __asm__ volatile("syscall" : "=a"(r) : "a"(0), "D"(0), "S"(&c), "d"(1)
                         : "rcx", "r11", "memory");
        if (r <= 0) break;
        if (c == '\n') break;
        if (n < 4095) buf[n++] = c;
    }
    buf[n] = 0;
    if (typeId == 5) {
        for (int i = 0; i <= n; ++i) strbuf[i] = buf[i];
        *slot = reinterpret_cast<unsigned long long>(strbuf);
        // Kata-1 (len) di 8 byte LEBIH RENDAH dari ptr slot (konvensi
        // fat16 View {ptr, len}: len di slot-8) — slot[1] salah arah.
        slot[-1] = static_cast<unsigned long long>(n);
        return;
    }
    if (typeId == 2) {
        *slot = static_cast<unsigned long long>(static_cast<unsigned char>(buf[0]));
        return;
    }
    if (typeId == 3) {
        *slot = (n > 0 && (buf[0] == 'b' || buf[0] == 'B' ||
                           buf[0] == 't' || buf[0] == 'T' ||
                           buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y'))
                    ? 1ULL : 0ULL;
        return;
    }
    int i = 0;
    while (i < n && (buf[i] == ' ' || buf[i] == '\t')) ++i;
    bool neg = false;
    if (i < n && (buf[i] == '-' || buf[i] == '+')) {
        neg = (buf[i] == '-');
        ++i;
    }
    unsigned long long ip = 0;
    while (i < n && buf[i] >= '0' && buf[i] <= '9') {
        ip = ip * 10ULL + static_cast<unsigned long long>(buf[i] - '0');
        ++i;
    }
    if (typeId == 4) {
        double v = static_cast<double>(ip);
        if (i < n && buf[i] == '.') {
            ++i;
            unsigned long long fp = 0, scale = 1;
            while (i < n && buf[i] >= '0' && buf[i] <= '9') {
                fp = fp * 10ULL + static_cast<unsigned long long>(buf[i] - '0');
                scale *= 10ULL;
                ++i;
            }
            v += static_cast<double>(fp) / static_cast<double>(scale);
        }
        if (neg) v = -v;
        double dv = v;
        unsigned long long bits;
        __builtin_memcpy(&bits, &dv, 8);
        *slot = bits;
        return;
    }
    long long sv = static_cast<long long>(ip);
    if (neg) sv = -sv;
    *slot = static_cast<unsigned long long>(sv);
}
