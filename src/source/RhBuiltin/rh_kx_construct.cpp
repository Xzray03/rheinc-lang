#include "rh_kx.h"

// kx(basis=..., real="...", imajiner="...") -> kompleks. Named args
// basis/skala di-drop jalur variadic; urutan arg = urutan penulisan.
extern "C" struct KXView rh_kx_construct(const char* rp, long rl,
                                         const char* ip, long il) {
    return kx_pack(kx_parse(rp, rl), kx_parse(ip, il));
}
