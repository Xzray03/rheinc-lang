// rh_sort.cpp - urutkan(arr, arah): bubble sort in-place pada slot
// array. Konvensi slot Rheinc menurun: elemen ke-i di base - i*esz.
// esz: 1/2/4/8 (integer signed) atau 16 (View teks {ptr,len}).
// arah: 0 = kecil ke besar, 1 = besar ke kecil.
extern "C" void rh_sort(char* base, long n, long esz, long arah) {
    // cmp(ea, eb) < 0 bila ea harus mendahului eb utk arah 0.
    auto cmp = [&](char* ea, char* eb) -> int {
        if (esz == 16) {
            // View teks: lexicographic byte compare
            const char* pa = *(const char**)ea;
            long la = *(long*)(ea - 8);   // konvensi slot menurun
            const char* pb = *(const char**)eb;
            long lb = *(long*)(eb - 8);
            long m = la < lb ? la : lb;
            for (long k = 0; k < m; ++k) {
                unsigned char ca = (unsigned char)pa[k];
                unsigned char cb = (unsigned char)pb[k];
                if (ca != cb) return ca < cb ? -1 : 1;
            }
            if (la != lb) return la < lb ? -1 : 1;
            return 0;
        }
        long va, vb;
        if (esz == 1) {
            va = *(signed char*)ea; vb = *(signed char*)eb;
        } else if (esz == 2) {
            va = *(short*)ea; vb = *(short*)eb;
        } else if (esz == 4) {
            va = *(int*)ea; vb = *(int*)eb;
        } else {
            va = *(long*)ea; vb = *(long*)eb;
        }
        return va < vb ? -1 : (va > vb ? 1 : 0);
    };
    auto swapElems = [&](char* ea, char* eb) {
        // Area elemen: esz<=8 -> [ea .. ea+esz-1] (menaik dlm elemen);
        // esz=16 (View) -> [ea-8 .. ea+7] (kata-1/len di bawah kata-0/
        // ptr, konvensi slot menurun). Copy dari awal area yg benar.
        char* aa = (esz > 8) ? (ea - (esz - 8)) : ea;
        char* bb = (esz > 8) ? (eb - (esz - 8)) : eb;
        char tmp[24];
        for (long k = 0; k < esz; ++k) tmp[k] = aa[k];
        for (long k = 0; k < esz; ++k) aa[k] = bb[k];
        for (long k = 0; k < esz; ++k) bb[k] = tmp[k];
    };
    for (long i = 0; i < n; ++i) {
        for (long j = 0; j + 1 < n - i; ++j) {
            char* ea = base - j * esz;        // elemen ke-j
            char* eb = base - (j + 1) * esz;  // elemen ke-j+1
            int c = cmp(ea, eb);
            // arah 0 (kecil->besar): swap bila ea > eb
            // arah 1 (besar->kecil): swap bila ea < eb
            if ((arah == 0 && c > 0) || (arah == 1 && c < 0)) {
                swapElems(ea, eb);
            }
        }
    }
}
