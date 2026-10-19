// rh_print_uint.cpp - Runtime: print bilangan tanpa tanda (u64) ke stdout.
// Pola sama rh_print_int.cpp (syscall write langsung, tanpa libc).
extern "C" void rh_print_uint(unsigned long long value) {
    char buffer[20];
    char* end = buffer + 20;
    char* p = end;
    do {
        *--p = static_cast<char>('0' + (value % 10ULL));
        value /= 10ULL;
    } while (value != 0ULL);
    const unsigned long n = static_cast<unsigned long>(end - p);
    __asm__ volatile("syscall" :
                     :
                     "a"(1), "D"(1), "S"(p), "d"(n) :
                     "rcx", "r11", "memory");
}
