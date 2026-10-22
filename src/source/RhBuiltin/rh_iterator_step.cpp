// rh_iterator_step.cpp - langkah iterator maju/mundur.
#include "rh_iter_helper.h"

extern "C" struct SysVView rh_iterator_berikutnya(long long it, long esz) {
    SysVView r = { (char*)0, 1 };   // KOSONG
    char* arr = *(char**)it;
    long p = iter_node_packed(it);
    long i = idx_of(p), c = cnt_of(p);
    if (iter_node_kind(it) == 1) {          // koleksi maju: node {next@0, data@8}
        if (!arr || i >= c) return r;
        iter_node_set_packed(it, ((i + 1) << 32) | c);
        iter_node_set_arr(it, *(void**)arr);
        r.ptr = (char*)0;                    // ADA
        r.packed = *(long*)((char*)arr + 8);
        return r;
    }
    if (i >= c) return r;                  // larik inline (kind 0)
    iter_node_set_packed(it, ((i + 1) << 32) | c);
    r.ptr = (char*)0;
    r.packed = *(long*)(arr - i * esz);     // kata-i di base-8i (MENURUN)
    return r;
}
extern "C" struct SysVView rh_iterator_sebelumnya(long long it, long esz) {
    SysVView r = { (char*)0, 1 };
    char* arr = *(char**)it;                 // kind 2: arr = HEAD, tak termutasi
    long p = iter_node_packed(it);
    long i = idx_of(p), c = cnt_of(p);
    if (iter_node_kind(it) == 2) {          // koleksi mundur: walk head -> node j
        if (i <= 0) return r;
        long j = i - 1;
        char* node = arr;
        for (long t = 0; t < j && node; t++) node = *(char**)node;
        if (!node) return r;
        iter_node_set_packed(it, (j << 32) | c);
        r.ptr = (char*)0;
        r.packed = *(long*)(node + 8);
        return r;
    }
    if (i <= 0) return r;                    // larik inline (kind 0)
    iter_node_set_packed(it, ((i - 1) << 32) | c);
    r.ptr = (char*)0;
    r.packed = *(long*)(arr - (i - 1) * esz);
    return r;
}
