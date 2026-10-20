// rh_str_to_bool.cpp - ke_logika(s): parse teks (ptr,len) ke logika.
// "benar"/"true"/"1"/"ya" (case-insensitive cukup lowercase) -> 1;
// "salah"/"false"/"0"/"tidak" -> 0; selainnya 0.
extern "C" long long rh_str_to_bool(const char* p, long len) {
    if (len <= 0) return 0;
    char c = p[0];
    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
    switch (c) {
        case 'b': case 't': case 'y': case '1':
            return 1;
        default:
            return 0;
    }
}
