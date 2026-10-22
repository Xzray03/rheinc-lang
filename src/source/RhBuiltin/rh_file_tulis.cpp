// rh_file_tulis.cpp - tulis_file(path, konten) -> sukses 1/0.
#include <fcntl.h>

extern "C" long rh_sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6);
extern "C" char* rh_npath(const char* p, long n);

extern "C" long long rh_write_file(const char* p, long plen,
                                   const char* c, long clen) {
    char* path = rh_npath(p, plen);
    if (!path) return 0;
    long fd = rh_sc6(2, (long)path, 0x241, 0644, 0, 0, 0);  // WRONLY|CREAT|TRUNC
    if (fd < 0) return 0;
    long w = rh_sc6(1, fd, (long)c, clen, 0, 0, 0);
    rh_sc6(3, fd, 0, 0, 0, 0, 0);
    return w == clen ? 1 : 0;
}
