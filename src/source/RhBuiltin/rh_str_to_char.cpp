// rh_str_to_char.cpp - ke_karakter(s): teks (ptr,len) -> karakter
// (byte pertama; kosong -> 0).
extern "C" long long rh_str_to_char(const char* p, long len) {
    if (len <= 0) return 0;
    return (long long)(unsigned char)p[0];
}
