// rh_unwrap.cpp - ambil_ada(opt): kembalikan payload Opsi (kata ke-1
// setelah tagValue). Dipanggil via call handler fat16-args (tag,
// payload -> dua register) -> return payload di rax.
extern "C" long long rh_unwrap_ada(long long tag, long long payload) {
    (void)tag;
    return payload;
}
