// rh_koleksi_larik.cpp - konversi koleksi <-> larik (2 varian ke_larik).
#include "rh_kol_helper.h"

// sized: salin head->tail LANGSUNG ke slot larik inline (elemen i di
// dest-i*esz, konvensi frontend). Void: data sudah di slot.
extern "C" void rh_tumpukan_ke_larik(char* slot, char* dest, long esz) {
    long eszA = esz > 0 ? esz : 8;
    long n = *rh_node_count(slot);
    long node = *rh_node_head(slot);
    for (long i = 0; i < n && node; i++) {
        rh_bdata_salin(dest - i * eszA, (const char*)(node + 8), eszA);
        node = *(long*)node;
    }
}
// unsized (View {ptr,len}): salin head->tail NAIK ke buffer arena.
extern "C" struct SysVView rh_tumpukan_ke_larik_view(char* slot, long esz) {
    long eszA = esz > 0 ? esz : 8;
    long n = *rh_node_count(slot);
    char* raw = (char*)rh_arena_alloc(n > 0 ? n * eszA : 1);
    long node = *rh_node_head(slot);
    for (long i = 0; i < n && node; i++) {
        rh_bdata_salin(raw + i * eszA, (const char*)(node + 8), eszA);
        node = *(long*)node;
    }
    SysVView r = { raw, n };
    return r;
}
