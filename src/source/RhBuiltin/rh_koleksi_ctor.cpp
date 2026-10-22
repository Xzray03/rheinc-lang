// rh_koleksi_ctor.cpp - konstruktor state kosong {head=0, count=0}.
#include "rh_kol_helper.h"

extern "C" struct SysVView rh_daftar()    { SysVView r = {0,0}; return r; }
extern "C" struct SysVView rh_antrian()   { SysVView r = {0,0}; return r; }
extern "C" struct SysVView rh_tumpukan()  { SysVView r = {0,0}; return r; }
