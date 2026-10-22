// rh_iterator_selesai.cpp - uji habis iterator (kind 2 = mundur).
#include "rh_iter_helper.h"

extern "C" long long rh_iterator_selesai(long long it) {
    long p = iter_node_packed(it);
    if (iter_node_kind(it) == 2) return idx_of(p) <= 0 ? 1 : 0;
    return idx_of(p) >= cnt_of(p) ? 1 : 0;
}
