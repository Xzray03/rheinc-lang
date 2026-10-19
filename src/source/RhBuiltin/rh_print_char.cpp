// rh_print_char.cpp - Runtime: print satu karakter ke stdout.
// Pola syscall write langsung, tanpa libc.
extern "C" void rh_print_char(unsigned long long ch) {
    char c = static_cast<char>(ch & 0xFFULL);
    __asm__ volatile("syscall" :
                     :
                     "a"(1), "D"(1), "S"(&c), "d"(1) :
                     "rcx", "r11", "memory");
}
