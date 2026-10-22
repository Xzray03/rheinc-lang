// rh_daftar_tambah.cpp - tambah PREPEND node (O(1)); basis semua "tambah".
#include "rh_kol_helper.h"

extern "C" void rh_daftar_tambah(char* slot, long data, long esz) {
    (void)esz;
    char* n = (char*)rh_arena_alloc(16);
    rh_node_set(n, *rh_node_head(slot), data);
    *rh_node_head(slot) = (long)n;
    *rh_node_count(slot) = *rh_node_count(slot) + 1;
}
