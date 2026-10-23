// rh_iter_helper.h - deklarasi bersama file rh_iterator_* & rh_iter_node.
// Aturan runtime: lihat RuntimeContract.h (standar RhBuiltin).
// Header murni (nol kode): semua fungsi di sini didefinisikan di
// rh_iter_node.cpp (linkage eksternal -> satu copy per binary).
#pragma once

struct SysVView { char* ptr; long packed; };   // packed = kata data / tag

extern "C" long long rh_arena_alloc(long sz);
long iter_node_kind(long long it);
long iter_node_packed(long long it);
void iter_node_set_packed(long long it, long p);
void iter_node_set_arr(long long it, void* a);

long idx_of(long p);
long cnt_of(long p);
