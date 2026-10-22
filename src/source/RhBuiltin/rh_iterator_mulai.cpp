// rh_iterator_mulai.cpp - konstruktor iterator AWAL (larik & koleksi).
#include "rh_iter_helper.h"

extern "C" long long rh_iterator_mulai(const void* arr, long count) {
    char* n = (char*)rh_arena_alloc(24);
    *(const void**)n = arr;
    *(long*)(n + 8) = (0L << 32) | (count & 0xFFFFFFFFL);
    *(long*)(n + 16) = 0;
    return (long long)n;
}
extern "C" long long rh_iterator_mulai_koleksi(const void* head, long count) {
    char* n = (char*)rh_arena_alloc(24);
    *(const void**)n = head;
    *(long*)(n + 8) = (0L << 32) | (count & 0xFFFFFFFFL);
    *(long*)(n + 16) = 1;
    return (long long)n;
}
