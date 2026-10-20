// rh_slice_str.cpp - potong(t, awal [sampai akhir]): cari pembatas
// dalam teks, persempit View t in-place. Layout slot View: ptr di
// slot[0..7], len di slot[-8..-1] (konvensi menurun).
// p2/l2 == 0: potong dari pembatas-1 sampai akhir teks.
static long find_sub(const char* hay, long hn,
                     const char* ndl, long nn) {
    if (nn <= 0 || hn < nn) return -1;
    for (long i = 0; i + nn <= hn; ++i) {
        long k = 0;
        while (k < nn && hay[i + k] == ndl[k]) ++k;
        if (k == nn) return i;
    }
    return -1;
}
extern "C" void rh_slice_str(char* slot,
                             const char* p1, long l1,
                             const char* p2, long l2) {
    const char* ptr = *(const char**)slot;
    long len = *(long*)(slot - 8);
    // Semantik korpus: KEDUA pembatas INCLUSIVE.
    // Uji 1: potong(a,"aku" sampai "rumah") -> "aku pulang ke rumah".
    // Pembatas-1 tak ditemukan -> TIDAK BERUBAH (bukan kosong).
    long i1 = find_sub(ptr, len, p1, l1);
    if (i1 < 0) return;                       // tidak berubah
    long start = i1;                          // inclusive pembatas-1
    long end;
    if (p2 != 0 && l2 > 0) {
        long i2 = find_sub(ptr + start, len - start, p2, l2);
        end = (i2 < 0) ? len : start + i2 + l2;   // inclusive pembatas-2
    } else {
        end = len;
    }
    *(const char**)slot = ptr + start;
    *(long*)(slot - 8) = end - start;
}
