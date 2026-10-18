  # Rheinc RhBackend - x86_64-linux-sysv (naive stack-slot)
  .text
  .globl utama
  .type utama,@function
  .align 16
utama:
  pushq %rbp
  movq %rsp, %rbp
  subq $64, %rsp
.LB0_utama:
  leaq .LC0_utama(%rip), %rax
  movq %rax, -16(%rbp)
  movq $13, %rax
  movq %rax, -24(%rbp)
  leaq .LC0_utama(%rip), %rdi
  movq $13, %rsi
  call rh_print
  leaq .LC2_utama(%rip), %rax
  movq %rax, -48(%rbp)
  movq $26, %rax
  movq %rax, -56(%rbp)
  leaq .LC2_utama(%rip), %rdi
  movq $26, %rsi
  call rh_print
  leave
  retq
  .size utama, .-utama
  .globl __rheinc_stub
  .type __rheinc_stub,@function
__rheinc_stub:
  call utama
  xorl %edi, %edi
  movq $60, %rax
  syscall
  .globl __global_init
  .type __global_init,@function
__global_init:
  retq
  .section .rodata
  .align 1
.LC0_utama:
  .asciz "Halo, Dunia!\n"
  .align 1
.LC2_utama:
  .asciz "Selamat datang di RheinC!\n"
  .section .note.GNU-stack,"",@progbits
