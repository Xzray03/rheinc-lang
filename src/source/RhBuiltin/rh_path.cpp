// rh_path.cpp - konversi path teks (ptr,len) -> NUL-terminated di arena.
// Linkage eksternal: satu objek (dipakai ketiga fungsi rh_file_*).
extern "C" void* rh_arena_alloc(unsigned long long size);

extern "C" char* rh_npath(const char* p, long n) {
    char* s = (char*)rh_arena_alloc((unsigned long long)(n + 1));
    if (!s) return 0;
    for (long i = 0; i < n; ++i) s[i] = p[i];
    s[n] = 0;
    return s;
}
