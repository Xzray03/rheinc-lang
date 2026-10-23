// rh_iter_node.cpp - helper node state iterator (24B: arr, packed, kind).
// Linkage eksternal: satu objek, satu copy di binary (dipakai semua
// fungsi rh_iterator_*; jangan static per-file -> duplikasi antar objek).
extern "C" long long rh_arena_alloc(long sz);   // didefinisikan di rh_arena.cpp

long iter_node_kind(long long it) { return *(long*)((char*)it + 16); }
long iter_node_packed(long long it) { return *(long*)((char*)it + 8); }
void iter_node_set_packed(long long it, long p) { *(long*)((char*)it + 8) = p; }
void iter_node_set_arr(long long it, void* a) { *(void**)((char*)it) = a; }

long idx_of(long p) { return (long)((unsigned long)p >> 32); }
long cnt_of(long p) { return (long)(p & 0xFFFFFFFFL); }
