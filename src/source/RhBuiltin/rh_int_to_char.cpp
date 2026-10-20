// rh_int_to_char.cpp - ke_karakter(bilangan): int -> karakter (low byte).
extern "C" long long rh_int_to_char(long long v) {
    return v & 0xFF;
}
