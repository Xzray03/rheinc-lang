// rh_sc6.cpp - syscall wrapper 6-arg generik (Linux x86-64).
// Linkage eksternal: satu objek. Dipakai rh_file_*; siap dipakai ulang
// runtime lain (futex/clone/rt_sigaction untuk concurrency nanti).
extern "C" long rh_sc6(long n, long a1, long a2, long a3,
                       long a4, long a5, long a6) {
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    long ret;
    __asm__ volatile("syscall"
        : "=a"(ret)
        : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return ret;
}
