struct SysVView { char* ptr; long len; };
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_float_to_text(long long bits) {
    char* buf = (char*)rh_arena_alloc(40);
    double d;
    __builtin_memcpy(&d, &bits, 8);
    long ip = (long)d;
    double frac = d - (double)ip;
    if (frac < 0) frac = -frac;
    long f6 = (long)(frac * 1000000.0 + 0.5);
    if (f6 >= 1000000) { ip += 1; f6 -= 1000000; }
    int p = 0;
    if (ip < 0) { buf[p++] = '-'; ip = -ip; }
    char tmp[20]; int n = 0;
    if (ip == 0) tmp[n++] = '0';
    while (ip > 0) { tmp[n++] = (char)('0' + ip % 10); ip /= 10; }
    while (n) buf[p++] = tmp[--n];
    buf[p++] = '.';
    for (int k = 100000; k > 0; k /= 10)
        buf[p++] = (char)('0' + (f6 / k) % 10);
    SysVView r = { buf, p };
    return r;
}
