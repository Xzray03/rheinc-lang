// rh_kol_helper.h - deklarasi bersama file rh_koleksi_* & rh_daftar_*.
// Aturan runtime: lihat RuntimeContract.h (standar RhBuiltin).
// Header murni (nol kode); definisi di rh_kol_node.cpp (satu objek).
#pragma once

struct SysVView { char* ptr; long packed; };   // packed = kata data / tag

extern "C" void* rh_arena_alloc(unsigned long long size);

void rh_node_set(char* node, long next, long data);
long* rh_node_head(char* slot);
long* rh_node_count(char* slot);
bool rh_bdata_sama(const char* a, const char* b, long esz);
void rh_bdata_salin(char* d, const char* s, long esz);
