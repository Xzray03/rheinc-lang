// rh_daftar_hapus.cpp - hapus belakang (walk ke prev-tail); depan
// (pop head) = identik antrian_ambil -> delegasi, jangan duplikasi.
#include "rh_kol_helper.h"

extern "C" struct SysVView rh_antrian_ambil(char* slot, long esz);
extern "C" struct SysVView rh_daftar_hapus_depan(char* slot, long esz) {
    return rh_antrian_ambil(slot, esz);
}
extern "C" struct SysVView rh_daftar_hapus_belakang(char* slot, long esz) {
    (void)esz;
    SysVView r = { (char*)0, 1 };   // KOSONG
    long head = *rh_node_head(slot);
    if (!head) return r;
    char* prev = 0;
    char* cur = (char*)head;
    while (*(long*)cur) { prev = cur; cur = (char*)*(long*)cur; }
    if (prev) *(long*)prev = 0; else *rh_node_head(slot) = 0;
    *rh_node_count(slot) -= 1;
    r.ptr = (char*)0;               // ADA
    r.packed = *(long*)(cur + 8);
    return r;
}
