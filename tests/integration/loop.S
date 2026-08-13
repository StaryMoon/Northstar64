  .option norvc
  .option norelax
  .section .text.boot
  .globl _start

_start:
  addi t0, t0, 1
  j _start

