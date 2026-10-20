// rh_memcpy.cpp - salin(dst, src, n): copy n byte. Tanpa libc.
extern "C" void rh_memcpy(void* dst, const void* src, long n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (long i = 0; i < n; ++i) d[i] = s[i];
}
