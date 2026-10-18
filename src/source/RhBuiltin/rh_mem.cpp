// rh_mem.cpp - Runtime alokasi memori Rheinc (RhBuiltin).
//
// Konvensi blok: [quad: len bytes data][data ...] — panjang di -8(ptr),
// sama dengan layout Alloc statis RhBackend sehingga Len seragam.

#include <cstdint>

extern "C" void* rh_alloc(std::int64_t sizeBytes) {
    // Sementara: area statis sederhana (bump) — cukup untuk fase ini.
    static char area[1 << 20];
    static std::int64_t used = 0;
    const std::int64_t total = 8 + sizeBytes;
    const std::int64_t off = used;
    used += (total + 15) / 16 * 16;
    if (used > (1 << 20)) return nullptr;  // kehabisan: null (poison)
    char* p = area + off;
    *reinterpret_cast<std::int64_t*>(p) = sizeBytes;  // header len
    return p + 8;
}

extern "C" void* rh_realloc(void* ptr, std::int64_t newSize) {
    if (ptr == nullptr) return rh_alloc(newSize);
    // Bump allocator: alokasi baru; header lama tetap (data lama tak disalin
    // secara otomatis — pemakaian Rheinc biasanya realokasi setelah alloc
    // segar).
    return rh_alloc(newSize);
}

extern "C" void rh_free(void* /*ptr*/) {
    // No-op: bump allocator tak mendukung pembebasan individual.
}
