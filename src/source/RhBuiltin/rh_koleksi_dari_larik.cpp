// rh_koleksi_dari_larik.cpp - bangun koleksi dari larik; larik[0] =
// head (top). Basis tumpukan; antrian/daftar_dari_larik delegasi.
#include "rh_kol_helper.h"

extern "C" struct SysVView rh_tumpukan_dari_larik(const char* base, long n, long esz) {
    long eszA = esz > 0 ? esz : 8;
    long head = 0;
    for (long i = n - 1; i >= 0; i--) {
        char* node = (char*)rh_arena_alloc(16);
        rh_node_set(node, head, 0);
        rh_bdata_salin((char*)(node + 8), base - i * eszA, eszA);
        head = (long)node;
    }
    SysVView r = { (char*)head, n };
    return r;
}
extern "C" struct SysVView rh_daftar_dari_larik(const char* base, long n, long esz) {
    return rh_tumpukan_dari_larik(base, n, esz);
}
