// rh_file.cpp - Runtime file I/O tanpa libc (syscall langsung).
// Path di-pass sebagai (ptr, len) — disalin ke arena + NUL dulu.
extern "C" void* rh_arena_alloc(unsigned long long size);

static long sc6(long n, long a1, long a2, long a3,
                long a4, long a5, long a6) {
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    long ret;
    __asm__ volatile("syscall" : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3),
                       "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static char* npath(const char* p, long n) {
    char* s = reinterpret_cast<char*>(rh_arena_alloc(
        static_cast<unsigned long long>(n + 1)));
    if (!s) return 0;
    for (long i = 0; i < n; ++i) s[i] = p[i];
    s[n] = 0;
    return s;
}

// tulis_file(path, konten) -> sukses (1/0)
extern "C" long long rh_write_file(const char* p, long plen,
                                   const char* c, long clen) {
    char* path = npath(p, plen);
    if (!path) return 0;
    long fd = sc6(2, (long)path, 0x241, 0644, 0, 0, 0);  // O_WRONLY|CREAT|TRUNC
    if (fd < 0) return 0;
    long w = sc6(1, fd, (long)c, clen, 0, 0, 0);
    sc6(3, fd, 0, 0, 0, 0, 0);
    return w == clen ? 1 : 0;
}

// baca_file(path, isi: View* via slot) -> sukses (1/0)
// Konvensi slot fat16: kata-0 ptr di *slot, kata-1 len di slot[-1].
extern "C" long long rh_read_file(const char* p, long plen,
                                  unsigned long long* slot) {
    char* path = npath(p, plen);
    if (!path) return 0;
    long fd = sc6(2, (long)path, 0, 0, 0, 0, 0);  // O_RDONLY
    if (fd < 0) return 0;
    long sz = sc6(8, fd, 0, 2, 0, 0, 0);          // lseek END
    if (sz < 0) { sc6(3, fd, 0, 0, 0, 0, 0); return 0; }
    sc6(8, fd, 0, 0, 0, 0, 0);                     // lseek SET 0
    char* buf = reinterpret_cast<char*>(rh_arena_alloc(
        static_cast<unsigned long long>(sz + 1)));
    long total = 0;
    while (total < sz) {
        long r = sc6(0, fd, (long)buf + total, sz - total, 0, 0, 0);
        if (r <= 0) break;
        total += r;
    }
    sc6(3, fd, 0, 0, 0, 0, 0);
    *slot = reinterpret_cast<unsigned long long>(buf);
    slot[-1] = static_cast<unsigned long long>(total);
    return 1;
}

// hapus_file(path) -> sukses (1/0)
extern "C" long long rh_unlink(const char* p, long plen) {
    char* path = npath(p, plen);
    if (!path) return 0;
    return sc6(87, (long)path, 0, 0, 0, 0, 0) == 0 ? 1 : 0;  // unlink
}
