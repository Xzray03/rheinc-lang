// rh_koleksi_tambah.cpp - delegasi: antrian_tambah & tumpukan_tambah
// identik dengan daftar_tambah (prepend). Jangan duplikasi.
#include "rh_kol_helper.h"

extern "C" void rh_daftar_tambah(char* slot, long data, long esz);
extern "C" void rh_antrian_tambah(char* slot, long data, long esz) {
    rh_daftar_tambah(slot, data, esz);   // enqueue prepend
}
extern "C" void rh_tumpukan_tambah(char* slot, long data, long esz) {
    rh_daftar_tambah(slot, data, esz);   // push = prepend
}
