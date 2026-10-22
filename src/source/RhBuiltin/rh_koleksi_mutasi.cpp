// rh_koleksi_mutasi.cpp - reset state ke kosong.
#include "rh_kol_helper.h"

extern "C" void rh_tumpukan_bersihkan(char* slot) {
    *rh_node_head(slot) = 0;
    *rh_node_count(slot) = 0;
}
