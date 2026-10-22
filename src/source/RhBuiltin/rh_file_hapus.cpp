// rh_file_hapus.cpp - hapus_file(path) -> sukses 1/0.
extern "C" long rh_sc6(long n, long a1, long a2, long a3, long a4, long a5, long a6);
extern "C" char* rh_npath(const char* p, long n);

extern "C" long long rh_unlink(const char* p, long plen) {
    char* path = rh_npath(p, plen);
    if (!path) return 0;
    return rh_sc6(87, (long)path, 0, 0, 0, 0, 0) == 0 ? 1 : 0;
}
