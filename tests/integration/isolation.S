  .option norvc
  .option norelax

  .equ UART_BASE,         0x10000000
  .equ RAM_BASE,          0x80000000
  .equ USER_TEXT_VA,      0x0000000040000000
  .equ USER_DATA_VA,      0x0000000040001000
  .equ USER_RO_VA,        0x0000000040002000
  .equ USER_STACK_VA,     0x0000000040003000
  .equ FORBIDDEN_VA,      0x0000000040004000

  .equ PTE_V,             0x001
  .equ PTE_R,             0x002
  .equ PTE_W,             0x004
  .equ PTE_X,             0x008
  .equ PTE_U,             0x010
  .equ PTE_A,             0x040
  .equ PTE_D,             0x080

  .equ CAUSE_ECALL_U,     8
  .equ CAUSE_LOAD_PAGE,   13
  .equ CAUSE_STORE_PAGE,  15

  .section .text.boot
  .globl _start
_start:
  la sp, machine_stack_top

  la t0, machine_trap
  csrw mtvec, t0

  # QEMU requires PMP permission before lower privilege can touch physical RAM.
  # Northstar64 does not claim PMP yet, so its precise illegal-instruction trap
  # skips these two probe writes while every later M-mode trap remains fatal.
  li t0, 1
  csrw mscratch, t0
  li t0, -1
  csrw pmpaddr0, t0
  li t0, 0x0f
  csrw pmpcfg0, t0
  csrw mscratch, zero

  li t0, (1 << CAUSE_ECALL_U) | (1 << CAUSE_LOAD_PAGE) | (1 << CAUSE_STORE_PAGE)
  csrw medeleg, t0

  la t0, supervisor_entry
  csrw mepc, t0
  csrr t1, mstatus
  li t2, 3
  slli t2, t2, 11
  not t2, t2
  and t1, t1, t2
  li t2, (1 << 11)
  or t1, t1, t2
  csrw mstatus, t1
  mret

machine_trap:
  csrr t0, mcause
  li t1, 2
  bne t0, t1, machine_trap_fatal
  csrr t0, mscratch
  li t1, 1
  bne t0, t1, machine_trap_fatal
  csrr t0, mepc
  addi t0, t0, 4
  csrw mepc, t0
  mret

machine_trap_fatal:
  la a0, msg_machine_fail
  jal ra, machine_puts
1:
  wfi
  j 1b

machine_puts:
  li t0, UART_BASE
1:
  lbu t1, 0(a0)
  beq t1, zero, 2f
  sb t1, 0(t0)
  addi a0, a0, 1
  j 1b
2:
  ret

  .section .text.supervisor
supervisor_entry:
  la sp, supervisor_stack_top
  la t0, supervisor_trap
  csrw stvec, t0

  # Root entry 0 identity-maps the QEMU virt MMIO window as a 1 GiB S-mode
  # leaf. Root entry 2 identity-maps RAM. Root entry 1 descends to the four
  # deliberately narrow U-mode mappings below.
  la t0, root_page_table
  li t1, PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D
  sd t1, 0(t0)

  la t2, level_one_user
  srli t2, t2, 12
  slli t2, t2, 10
  ori t2, t2, PTE_V
  sd t2, 8(t0)

  li t2, RAM_BASE
  srli t2, t2, 12
  slli t2, t2, 10
  or t2, t2, t1
  sd t2, 16(t0)

  la t0, level_one_user
  la t2, level_zero_user
  srli t2, t2, 12
  slli t2, t2, 10
  ori t2, t2, PTE_V
  sd t2, 0(t0)

  la t0, level_zero_user
  la t2, __user_text_phys
  srli t2, t2, 12
  slli t2, t2, 10
  li t3, PTE_V | PTE_R | PTE_X | PTE_U | PTE_A
  or t2, t2, t3
  sd t2, 0(t0)

  la t2, __user_data_phys
  srli t2, t2, 12
  slli t2, t2, 10
  li t3, PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D
  or t2, t2, t3
  sd t2, 8(t0)

  la t2, __user_ro_phys
  srli t2, t2, 12
  slli t2, t2, 10
  li t3, PTE_V | PTE_R | PTE_U | PTE_A
  or t2, t2, t3
  sd t2, 16(t0)

  la t2, __user_stack_phys
  srli t2, t2, 12
  slli t2, t2, 10
  li t3, PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D
  or t2, t2, t3
  sd t2, 24(t0)

  la t0, root_page_table
  srli t0, t0, 12
  li t1, 8
  slli t1, t1, 60
  or t0, t0, t1
  csrw satp, t0
  sfence.vma zero, zero

  la t0, user_entry
  li t1, USER_TEXT_VA
  la t2, user_text_start
  sub t0, t0, t2
  add t0, t0, t1
  csrw sepc, t0
  la t0, supervisor_stack_top
  csrw sscratch, t0
  li sp, USER_STACK_VA + 0x1000
  csrr t0, sstatus
  li t1, 1
  slli t1, t1, 8
  not t1, t1
  and t0, t0, t1
  csrw sstatus, t0
  la a0, msg_boot
  jal ra, supervisor_puts
  sret

supervisor_trap:
  csrrw sp, sscratch, sp
  addi sp, sp, -240
  sd ra,   0(sp)
  sd gp,   8(sp)
  sd tp,  16(sp)
  sd t0,  24(sp)
  sd t1,  32(sp)
  sd t2,  40(sp)
  sd s0,  48(sp)
  sd s1,  56(sp)
  sd a0,  64(sp)
  sd a1,  72(sp)
  sd a2,  80(sp)
  sd a3,  88(sp)
  sd a4,  96(sp)
  sd a5, 104(sp)
  sd a6, 112(sp)
  sd a7, 120(sp)
  sd s2, 128(sp)
  sd s3, 136(sp)
  sd s4, 144(sp)
  sd s5, 152(sp)
  sd s6, 160(sp)
  sd s7, 168(sp)
  sd s8, 176(sp)
  sd s9, 184(sp)
  sd s10, 192(sp)
  sd s11, 200(sp)
  sd t3, 208(sp)
  sd t4, 216(sp)
  sd t5, 224(sp)
  sd t6, 232(sp)

  csrr t0, scause
  li t1, CAUSE_ECALL_U
  beq t0, t1, handle_ecall
  li t1, CAUSE_LOAD_PAGE
  beq t0, t1, handle_load_page
  li t1, CAUSE_STORE_PAGE
  beq t0, t1, handle_store_page
  j supervisor_fail

handle_ecall:
  li t0, 1
  beq a7, t0, syscall_putchar
  li t0, 2
  beq a7, t0, syscall_exit
  j supervisor_fail

syscall_putchar:
  li t0, UART_BASE
  sb a0, 0(t0)
  csrr t0, sepc
  addi t0, t0, 4
  csrw sepc, t0
  j supervisor_return

syscall_exit:
  la t0, fault_phase
  ld t1, 0(t0)
  li t2, 2
  bne t1, t2, supervisor_fail
  la a0, msg_pass
  jal ra, supervisor_puts
1:
  wfi
  j 1b

handle_load_page:
  la t2, fault_phase
  ld t3, 0(t2)
  li t0, 0
  bne t3, t0, supervisor_fail
  csrr t0, stval
  li t1, FORBIDDEN_VA
  bne t0, t1, supervisor_fail
  li t3, 1
  sd t3, 0(t2)
  csrr t0, sepc
  addi t0, t0, 4
  csrw sepc, t0
  la a0, msg_load_fault
  jal ra, supervisor_puts
  j supervisor_return

handle_store_page:
  la t2, fault_phase
  ld t3, 0(t2)
  li t0, 1
  bne t3, t0, supervisor_fail
  csrr t0, stval
  li t1, USER_RO_VA
  bne t0, t1, supervisor_fail
  li t3, 2
  sd t3, 0(t2)
  csrr t0, sepc
  addi t0, t0, 4
  csrw sepc, t0
  la a0, msg_store_fault
  jal ra, supervisor_puts
  j supervisor_return

supervisor_return:
  ld ra,   0(sp)
  ld gp,   8(sp)
  ld tp,  16(sp)
  ld t0,  24(sp)
  ld t1,  32(sp)
  ld t2,  40(sp)
  ld s0,  48(sp)
  ld s1,  56(sp)
  ld a0,  64(sp)
  ld a1,  72(sp)
  ld a2,  80(sp)
  ld a3,  88(sp)
  ld a4,  96(sp)
  ld a5, 104(sp)
  ld a6, 112(sp)
  ld a7, 120(sp)
  ld s2, 128(sp)
  ld s3, 136(sp)
  ld s4, 144(sp)
  ld s5, 152(sp)
  ld s6, 160(sp)
  ld s7, 168(sp)
  ld s8, 176(sp)
  ld s9, 184(sp)
  ld s10, 192(sp)
  ld s11, 200(sp)
  ld t3, 208(sp)
  ld t4, 216(sp)
  ld t5, 224(sp)
  ld t6, 232(sp)
  addi sp, sp, 240
  csrrw sp, sscratch, sp
  sret

supervisor_fail:
  la a0, msg_supervisor_fail
  jal ra, supervisor_puts
1:
  wfi
  j 1b

supervisor_puts:
  li t0, UART_BASE
1:
  lbu t1, 0(a0)
  beq t1, zero, 2f
  sb t1, 0(t0)
  addi a0, a0, 1
  j 1b
2:
  ret

  .section .user.text,"ax"
user_text_start:
user_entry:
  li a0, 'U'
  li a7, 1
  ecall
  li a0, 'S'
  ecall
  li a0, 'E'
  ecall
  li a0, 'R'
  ecall
  li a0, '\n'
  ecall

  li t0, USER_DATA_VA
  li t1, 0x2a
  sd t1, 0(t0)
  ld t2, 0(t0)
  bne t1, t2, user_fail

  li t0, FORBIDDEN_VA
  ld t1, 0(t0)
load_resume:
  li t0, USER_RO_VA
  sd zero, 0(t0)
store_resume:
  li a7, 2
  ecall

user_fail:
  li a0, '!'
  li a7, 1
  ecall
  li a7, 2
  ecall
user_text_end:

  .section .rodata
msg_boot:
  .asciz "NS64:BOOT:M>S>U\n"
msg_load_fault:
  .asciz "NS64:TRAP:LOAD_PAGE:0x40004000\n"
msg_store_fault:
  .asciz "NS64:TRAP:STORE_PAGE:0x40002000\n"
msg_pass:
  .asciz "NS64:PASS:ISOLATION\n"
msg_machine_fail:
  .asciz "NS64:FAIL:MACHINE_TRAP\n"
msg_supervisor_fail:
  .asciz "NS64:FAIL:SUPERVISOR_TRAP\n"

  .section .user.data
  .align 3
user_data:
  .dword 0

  .section .user.rodata
  .align 3
user_read_only:
  .dword 0x4e533634

  .section .user.stack,"aw",@nobits
  .align 12
user_stack:
  .skip 4096

  .section .page_tables,"aw",@nobits
  .align 12
root_page_table:
  .skip 4096
level_one_user:
  .skip 4096
level_zero_user:
  .skip 4096

  .section .bss
  .align 3
fault_phase:
  .dword 0
  .align 4
machine_stack:
  .skip 4096
machine_stack_top:
supervisor_stack:
  .skip 4096
supervisor_stack_top:
