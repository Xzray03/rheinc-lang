// rh_daftar_tambah_belakang.cpp - append ke tail (walk O(n)).
#include "rh_kol_helper.h"

extern "C" void rh_daftar_tambah_belakang(char* slot, long data, long esz) {
    long eszA = esz > 0 ? esz : 8;
    char* node = (char*)rh_arena_alloc(16);
    rh_node_set(node, 0, 0);
    rh_bdata_salin((char*)(node + 8), (const char*)&data, eszA);
    long head = *rh_node_head(slot);
    if (!head) {
        *rh_node_head(slot) = (long)node;
    } else {
        char* tail = (char*)head;
        while (*(long*)tail) tail = (char*)*(long*)tail;
        *(long*)tail = (long)node;
    }
    *rh_node_count(slot) += 1;
}
