struct SysVView { char* ptr; long len; };
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_uint_to_text(unsigned long long u) {
    char* buf = (char*)rh_arena_alloc(24);
    char tmp[20]; int n = 0;
    if (u == 0) tmp[n++] = '0';
    while (u > 0) { tmp[n++] = (char)('0' + u % 10); u /= 10; }
    int p = 0;
    while (n) buf[p++] = tmp[--n];
    SysVView r = { buf, p };
    return r;
}
