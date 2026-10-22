// rh_kx.h - deklarasi bersama semua file rh_kx_* (header murni, nol kode).
// Aturan runtime: lihat RuntimeContract.h (standar RhBuiltin).
#pragma once

// kompleks (re, im) sebagai bit-pattern pasangan double 16B
struct KXView { long long re, im; };

// hasil konversi ke teks (View: {ptr, len})
struct SysVView { char* ptr; long packed; };

// ---- rh_kx_util.cpp ----
KXView kx_pack(double re, double im);
void kx_unpack(long long reb, long long imb, double& re, double& im);

// ---- rh_math_kx.cpp (math manual, tanpa libm) ----
double kx_sqrt(double x);
double kx_fabs(double x);
double kx_sin(double x);
double kx_cos(double x);
double kx_atan(double z);            // z >= 0
double kx_atan2(double y, double x);
double kx_exp(double x);
double kx_ln(double x);              // x > 0
double kx_pow(double b, double e);   // b > 0
double kx_sinh(double x);
double kx_cosh(double x);

// ---- rh_parse.cpp ----
double kx_parse(const char* p, long len);

// ---- rh_fmt.cpp ----
// format manual "%.<digits>f" -> out; kembalikan panjang (tanpa NUL).
int rh_fmt_double(double v, int digits, char* out);
