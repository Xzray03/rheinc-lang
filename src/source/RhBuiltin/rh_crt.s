# rh_crt.s - Startup runtime minimal Rheinc (RhBuiltin).
#
# Entry ELF _start memanggil simbol entry Rheinc ("utama"), lalu
# exit(0) via syscall mentah. Mengasumsikan stack System V standar.
    .text
    .globl _start
_start:
    call __global_init
    call __rheinc_stub  # stub: exit code deterministik (void->0, nilai->rax)
    hlt                 # tak tercapai (stub exit sendiri)
    .section .note.GNU-stack,"",@progbits
