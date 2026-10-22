// rh_math_kx.cpp - math engine manual (tanpa libm): sqrt Newton-Raphson,
// sin/cos/atan Taylor, ln via atanh-transform, exp/sinh/cosh turunan.
// Satu objek: fungsi saling memanggil berantai (pow->exp->ln, ...).
#include "rh_kx.h"

static const double KX_PI  = 3.14159265358979323846;
static const double KX_2PI = 6.28318530717958647692;

double kx_sqrt(double x) {              // Newton-Raphson
    if (x <= 0.0) return 0.0;
    double y = x > 1.0 ? x : 1.0;
    for (int k = 0; k < 60; ++k) y = 0.5 * (y + x / y);
    return y;
}
double kx_fabs(double x) { return x < 0.0 ? -x : x; }
static double kx_fmod2pi(double x) {
    while (x >  KX_PI) x -= KX_2PI;
    while (x < -KX_PI) x += KX_2PI;
    return x;
}
static double kx_pow_int(double b, int e) {
    double r = 1.0;
    bool neg = e < 0; if (neg) e = -e;
    for (int k = 0; k < e; ++k) r *= b;
    return neg ? 1.0 / r : r;
}
double kx_sin(double x) {
    x = kx_fmod2pi(x);
    double r = x, t = x;
    for (int k = 1; k <= 12; ++k) { t = -t * x * x / ((2.0 * k) * (2.0 * k + 1.0)); r += t; }
    return r;
}
double kx_cos(double x) {
    x = kx_fmod2pi(x);
    double r = 1.0, t = 1.0;
    for (int k = 1; k <= 12; ++k) { t = -t * x * x / ((2.0 * k - 1.0) * (2.0 * k)); r += t; }
    return r;
}
double kx_atan(double z) {              // z >= 0; identitas utk z > 1
    bool inv = false;
    if (z > 1.0) { z = 1.0 / z; inv = true; }
    double z2 = z * z, r = z, t = z;
    for (int k = 1; k <= 25; ++k) { t = -t * z2; r += t / (2.0 * k + 1.0); }
    return inv ? KX_PI / 2.0 - r : r;
}
double kx_atan2(double y, double x) {
    if (x == 0.0) return y > 0.0 ? KX_PI / 2.0 : (y < 0.0 ? -KX_PI / 2.0 : 0.0);
    double a = kx_atan(kx_fabs(y / x));
    if (x > 0.0) return y >= 0.0 ? a : -a;
    return y >= 0.0 ? KX_PI - a : -(KX_PI - a);
}
double kx_exp(double x) {
    if (x < 0.0) return 1.0 / kx_exp(-x);
    double r = 1.0, t = 1.0;
    for (int k = 1; k <= 40; ++k) { t *= x / k; r += t; }
    return r;
}
double kx_ln(double x) {               // ln x = 2*atanh((x-1)/(x+1))
    if (x <= 0.0) return 0.0;
    int scale = 0;
    while (x > 2.0) { x /= 2.71828182845904523536; ++scale; }
    while (x < 0.5) { x *= 2.71828182845904523536; --scale; }
    double z = (x - 1.0) / (x + 1.0), z2 = z * z, r = z, t = z;
    for (int k = 1; k <= 40; ++k) { t *= z2; r += t / (2.0 * k + 1.0); }
    return 2.0 * r + (double)scale;
}
double kx_pow(double b, double e) { return kx_exp(e * kx_ln(b)); }
double kx_sinh(double x) { return (kx_exp(x) - kx_exp(-x)) / 2.0; }
double kx_cosh(double x) { return (kx_exp(x) + kx_exp(-x)) / 2.0; }
