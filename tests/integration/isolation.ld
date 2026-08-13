OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
  text PT_LOAD FLAGS(5);
  data PT_LOAD FLAGS(6);
}

SECTIONS
{
  . = 0x80000000;
  .text : ALIGN(0x1000)
  {
    KEEP(*(.text.boot))
    *(.text.supervisor)
    *(.text .text.*)
  } :text

  .rodata : ALIGN(16)
  {
    *(.rodata .rodata.*)
  } :text

  . = ALIGN(0x1000);
  __user_text_phys = .;
  .user_text : ALIGN(0x1000)
  {
    *(.user.text)
  } :text

  . = ALIGN(0x1000);
  __user_data_phys = .;
  .user_data : ALIGN(0x1000)
  {
    *(.user.data)
  } :data

  . = ALIGN(0x1000);
  __user_ro_phys = .;
  .user_rodata : ALIGN(0x1000)
  {
    *(.user.rodata)
  } :data

  .data : ALIGN(16)
  {
    *(.data .data.*)
  } :data

  . = ALIGN(0x1000);
  __user_stack_phys = .;
  .user_stack (NOLOAD) : ALIGN(0x1000)
  {
    *(.user.stack)
  } :data

  .page_tables (NOLOAD) : ALIGN(0x1000)
  {
    *(.page_tables)
  } :data

  .bss (NOLOAD) : ALIGN(16)
  {
    *(.bss .bss.* COMMON)
  } :data

  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.riscv.attributes)
  }

  ASSERT(SIZEOF(.user_text) > 0, "user text must not be empty")
  ASSERT(SIZEOF(.user_text) <= 0x1000, "user text exceeds its mapped page")
  ASSERT(SIZEOF(.user_data) <= 0x1000, "user data exceeds its mapped page")
  ASSERT(SIZEOF(.user_rodata) <= 0x1000, "user rodata exceeds its mapped page")
  ASSERT(SIZEOF(.user_stack) == 0x1000, "user stack must occupy one mapped page")
}
