# rh_crt.s - Startup runtime minimal Rheinc (RhBuiltin).
#
# Entry ELF _start memanggil simbol entry Rheinc ("utama"), lalu
# exit(0) via syscall mentah. Mengasumsikan stack System V standar.
    .text
    .globl _start
_start:
    call __global_init
    call utama
    movq %rax, %rdi     # status = nilai kembalian utama
    movq $60, %rax      # SYS_exit
    syscall
    .section .note.GNU-stack,"",@progbits
