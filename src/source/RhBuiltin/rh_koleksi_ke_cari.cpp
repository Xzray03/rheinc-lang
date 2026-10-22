// rh_koleksi_ke_cari.cpp - akses indeks dari head & pencarian -> Opsi.
#include "rh_kol_helper.h"

extern "C" long rh_tumpukan_ke(char* slot, long i, long esz) {
    (void)esz;
    long node = *rh_node_head(slot);
    for (long k = 0; k < i && node; k++) node = *(long*)node;
    if (!node) return 0;
    return *(long*)(node + 8);
}
extern "C" struct SysVView rh_tumpukan_cari(char* slot, long data, long esz) {
    SysVView r = { (char*)0, 1 };   // KOSONG
    long node = *rh_node_head(slot);
    long i = 0;
    while (node) {
        if (rh_bdata_sama((const char*)(node + 8), (const char*)&data, esz)) {
            r.ptr = (char*)0;       // ADA
            r.packed = i;
            return r;
        }
        node = *(long*)node;
        i++;
    }
    return r;
}
