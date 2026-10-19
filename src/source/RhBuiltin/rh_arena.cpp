// rh_arena.cpp - Arena allocator dinamis: alokasi sesuai kebutuhan,
// tumbuh via mmap anon (1 MB per chunk, bukan batas ketat — batas
// hanya memori hardware). Tidak ada free (arena semantics): cocok
// utk buffer concat & file read yg lifetime = program.
extern "C" void* rh_arena_alloc(unsigned long long size) {
    static char* cur = 0;
    static char* end = 0;
    // align 16
    size = (size + 15ULL) & ~15ULL;
    if (cur + size > end) {
        unsigned long long chunk = size > 1048576ULL ? size : 1048576ULL;
        long r;
        // mmap(0, chunk, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON, -1, 0)
        register long r10 __asm__("r10") = 0x22;  // MAP_PRIVATE|MAP_ANONYMOUS
        register long r8 __asm__("r8") = 0;
        register long r9 __asm__("r9") = 0;
        __asm__ volatile("syscall"
                         : "=a"(r)
                         : "a"(9), "D"(0), "S"(chunk), "d"(3),
                           "r"(r10), "r"(r8), "r"(r9)
                         : "rcx", "r11", "memory");
        if (r < 0 && r > -4096L) return 0;  // mmap gagal
        cur = reinterpret_cast<char*>(r);
        end = cur + chunk;
    }
    void* p = cur;
    cur += size;
    return p;
}
