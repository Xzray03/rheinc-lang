// rh_concat.cpp - a +> b utk teks View: gabungkan ke buffer arena
// baru, kembalikan View {ptr, len} via rax:rdx (SysV ABI).
struct SysVView {
    char* ptr;
    long len;
};
extern "C" void* rh_arena_alloc(unsigned long long size);
extern "C" struct SysVView rh_concat(const char* a, long alen,
                                     const char* b, long blen) {
    SysVView v = { 0, 0 };
    if (alen < 0) alen = 0;
    if (blen < 0) blen = 0;
    char* buf = reinterpret_cast<char*>(rh_arena_alloc(
        static_cast<unsigned long long>(alen + blen + 1)));
    if (!buf) return v;
    for (long i = 0; i < alen; ++i) buf[i] = a[i];
    for (long i = 0; i < blen; ++i) buf[alen + i] = b[i];
    buf[alen + blen] = 0;
    v.ptr = buf;
    v.len = alen + blen;
    return v;
}
