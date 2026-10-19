// rh_print_bool.cpp - Runtime: print logika ke stdout ("benar"/"salah").
// Pola syscall write langsung, tanpa libc.
extern "C" void rh_print_bool(unsigned long long value) {
    static const char benar[] = "benar";
    static const char salah[] = "salah";
    const char* s = value != 0ULL ? benar : salah;
    const unsigned long n = 5UL;  // "benar" & "salah" sama-sama 5 huruf
    __asm__ volatile("syscall" :
                     :
                     "a"(1), "D"(1), "S"(s), "d"(n) :
                     "rcx", "r11", "memory");
}
