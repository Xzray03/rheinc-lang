// rh_koleksi_ambil.cpp - pop head -> Opsi {tag ADA=0 | KOSONG=1}.
// Basis: antrian_ambil; tumpukan_ambil & daftar_hapus_depan delegasi.
#include "rh_kol_helper.h"

extern "C" struct SysVView rh_antrian_ambil(char* slot, long esz) {
    (void)esz;
    SysVView r = { (char*)0, 1 };   // KOSONG
    long h = *rh_node_head(slot);
    if (!h) return r;
    long data = *(long*)(h + 8);
    *rh_node_head(slot) = *(long*)h;
    *rh_node_count(slot) = *rh_node_count(slot) - 1;
    r.ptr = (char*)0;               // ADA
    r.packed = data;
    return r;
}
extern "C" struct SysVView rh_tumpukan_ambil(char* slot, long esz) {
    return rh_antrian_ambil(slot, esz);   // pop = remove head
}
