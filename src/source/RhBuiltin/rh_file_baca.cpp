// rh_file_baca.cpp - baca_file(path, slot hasil) -> sukses 1/0.
// Konvensi slot fat16: kata-0 ptr di *slot, kata-1 len di slot[-1].
extern "C" long rh_sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6);
extern "C" char* rh_npath(const char* p, long n);
extern "C" void* rh_arena_alloc(unsigned long long size);

extern "C" long long rh_read_file(const char* p, long plen,
                                  unsigned long long* slot) {
    char* path = rh_npath(p, plen);
    if (!path) return 0;
    long fd = rh_sc6(2, (long)path, 0, 0, 0, 0, 0);  // O_RDONLY
    if (fd < 0) return 0;
    long sz = rh_sc6(8, fd, 0, 2, 0, 0, 0);         // lseek END
    if (sz < 0) { rh_sc6(3, fd, 0, 0, 0, 0, 0); return 0; }
    rh_sc6(8, fd, 0, 0, 0, 0, 0);                    // lseek SET 0
    char* buf = (char*)rh_arena_alloc((unsigned long long)(sz + 1));
    long total = 0;
    while (total < sz) {
        long r = rh_sc6(0, fd, (long)buf + total, sz - total, 0, 0, 0);
        if (r <= 0) break;
        total += r;
    }
    rh_sc6(3, fd, 0, 0, 0, 0, 0);
    *slot = (unsigned long long)buf;
    slot[-1] = (unsigned long long)total;
    return 1;
}
