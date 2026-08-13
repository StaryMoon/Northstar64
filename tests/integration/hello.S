  .option norvc
  .option norelax
  .section .text.boot
  .globl _start

_start:
  lui t0, 0x10000
  li t1, 'N'
  sb t1, 0(t0)
  li t1, 'S'
  sb t1, 0(t0)
  li t1, '6'
  sb t1, 0(t0)
  li t1, '4'
  sb t1, 0(t0)
  li t1, '\n'
  sb t1, 0(t0)

  li s0, 40
  addi s0, s0, 2
  la t2, result
  sd s0, 0(t2)
  ld s1, 0(t2)
  bne s0, s1, failure

  ebreak

failure:
  li t1, '!'
  sb t1, 0(t0)
  ebreak

  .section .bss
  .align 3
result:
  .dword 0

