// rh_koleksi_atas.cpp - lihat head tanpa menghapus -> Opsi.
#include "rh_kol_helper.h"

extern "C" struct SysVView rh_tumpukan_atas(char* slot, long esz) {
    (void)esz;
    SysVView r = { (char*)0, 1 };   // KOSONG
    long h = *rh_node_head(slot);
    if (!h) return r;
    r.ptr = (char*)0;               // ADA
    r.packed = *(long*)(h + 8);
    return r;
}
