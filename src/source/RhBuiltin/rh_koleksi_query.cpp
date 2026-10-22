// rh_koleksi_query.cpp - query state (identik antar tipe koleksi).
#include "rh_kol_helper.h"

extern "C" long rh_tumpukan_ukuran(char* slot) {
    return *rh_node_count(slot);
}
extern "C" long rh_tumpukan_kosong(char* slot) {
    return *rh_node_count(slot) == 0 ? 1 : 0;
}
