// rh_concat_array.cpp - gabung(a, b): copy byte-per-byte ke slot hasil
// call. Panjang dalam BYTE (bukan kata — elemen bisa 4-byte karakter).
// Konvensi slot Rheinc menurun: byte ke-i di base - i.
extern "C" void rh_concat_array_to(const void* a, long abytes,
                                   const void* b, long bbytes, void* out) {
    for (long i = 0; i < abytes; ++i) {
        ((char*)out)[-i] = ((const char*)a)[-i];
    }
    for (long i = 0; i < bbytes; ++i) {
        ((char*)out)[-(abytes + i)] = ((const char*)b)[-i];
    }
}
