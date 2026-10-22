// rh_kol_node.cpp - helper node & state koleksi (dipakai semua fungsi
// rh_koleksi_*/rh_daftar_*). Linkage eksternal: satu objek, satu copy.
#include "rh_kol_helper.h"

void rh_node_set(char* node, long next, long data) {
    *(long*)(node + 0) = next;
    *(long*)(node + 8) = data;
}
// head di slot[0], count di slot[-8] (konvensi slot menurun)
long* rh_node_head(char* slot) { return (long*)slot; }
long* rh_node_count(char* slot)  { return (long*)(slot - 8); }

bool rh_bdata_sama(const char* a, const char* b, long esz) {
    for (long k = 0; k < esz; k++) if (a[k] != b[k]) return false;
    return true;
}
void rh_bdata_salin(char* d, const char* s, long esz) {
    for (long k = 0; k < esz; k++) d[k] = s[k];
}
