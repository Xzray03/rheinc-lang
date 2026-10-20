struct SysVView { char* ptr; long len; };
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_int_to_text(long long v) {
    char* buf = (char*)rh_arena_alloc(24);
    int p = 0;
    if (v < 0) { buf[p++] = '-'; v = -v; }
    char tmp[20]; int n = 0;
    unsigned long long u = (unsigned long long)v;
    if (u == 0) tmp[n++] = '0';
    while (u > 0) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    while (n) buf[p++] = tmp[--n];
    SysVView r = { buf, p };
    return r;
}
