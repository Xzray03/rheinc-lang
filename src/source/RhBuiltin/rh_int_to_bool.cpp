// rh_int_to_bool.cpp - ke_logika(bilangan): truthy -> 1/0.
extern "C" long long rh_int_to_bool(long long v) {
    return v != 0 ? 1 : 0;
}
