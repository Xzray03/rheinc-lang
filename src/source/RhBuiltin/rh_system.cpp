// rh_system.cpp - Runtime: jalankan perintah shell (sh -c <cmd>),
// tangkap stdout & stderr ke buffer terpisah, kembalikan satu teks
// gabungan ber-header:
//   stdout:
//   <isi stdout>
//   stderr:
//   <isi stderr>
// Return View {ptr, len} via rax:rdx (SysV ABI struct <=16 byte).
// Tanpa libc: fork/pipe/dup2/execve/wait4.
// CATATAN: parent baca stdout sampai EOF lalu stderr — aman utk output
// < pipe buffer (64KB); output besar di satu stream bisa deadlock.
struct SysVView {
    char* ptr;
    long len;
};

static long sys_call6(long n, long a1, long a2, long a3,
                      long a4, long a5, long a6) {
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    long ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3),
                       "r"(r10), "r"(r8), "r"(r9)
                     : "rcx", "r11", "memory");
    return ret;
}

static long read_all(int fd, char* dst, long cap) {
    long total = 0;
    for (;;) {
        long r = sys_call6(0, fd, (long)dst + total, cap - total, 0, 0, 0);
        if (r <= 0) break;
        total += r;
        if (total >= cap) break;
    }
    return total;
}

extern "C" struct SysVView rh_system(const char* cmd) {
    static char outbuf[65536];
    static char errbuf[65536];
    static char result[132096];
    int fo[2], fe[2];  // pipe() menulis 2x int
    if (sys_call6(22, (long)fo, 0, 0, 0, 0, 0) != 0 ||   // pipe stdout
        sys_call6(22, (long)fe, 0, 0, 0, 0, 0) != 0) {   // pipe stderr
        SysVView v = { result, 0 };
        return v;
    }
    long pid = sys_call6(57, 0, 0, 0, 0, 0, 0);          // fork
    if (pid == 0) {
        sys_call6(33, fo[1], 1, 0, 0, 0, 0);             // dup2 out -> fd1
        sys_call6(33, fe[1], 2, 0, 0, 0, 0);             // dup2 err -> fd2
        sys_call6(3, fo[0], 0, 0, 0, 0, 0);              // close rd ends
        sys_call6(3, fe[0], 0, 0, 0, 0, 0);
        const char* argv[4];
        argv[0] = "/bin/sh"; argv[1] = "-c";
        argv[2] = cmd; argv[3] = 0;
        sys_call6(59, (long)"/bin/sh", (long)argv, 0, 0, 0, 0);  // execve
        sys_call6(60, 127, 0, 0, 0, 0, 0);               // exit(127)
    }
    sys_call6(3, fo[1], 0, 0, 0, 0, 0);                  // close wr ends
    sys_call6(3, fe[1], 0, 0, 0, 0, 0);
    long nout = read_all(fo[0], outbuf, 65535);
    sys_call6(3, fo[0], 0, 0, 0, 0, 0);
    long nerr = read_all(fe[0], errbuf, 65535);
    sys_call6(3, fe[0], 0, 0, 0, 0, 0);
    long st = 0;
    sys_call6(61, pid, (long)&st, 0, 0, 0, 0);           // wait4
    // Susun "stdout:\n<out>\nstderr:\n<err>"
    long p = 0;
    const char* h1 = "stdout:\n";
    for (const char* c = h1; *c; ++c) result[p++] = *c;
    for (long i = 0; i < nout; ++i) result[p++] = outbuf[i];
    if (nout == 0 || outbuf[nout - 1] != '\n') result[p++] = '\n';
    const char* h2 = "stderr:\n";
    for (const char* c = h2; *c; ++c) result[p++] = *c;
    for (long i = 0; i < nerr; ++i) result[p++] = errbuf[i];
    SysVView v = { result, p };
    return v;
}
