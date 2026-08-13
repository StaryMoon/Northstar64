OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;

  .text : ALIGN(16)
  {
    KEEP(*(.text.boot))
    *(.text .text.*)
  }

  .rodata : ALIGN(16)
  {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(16)
  {
    *(.data .data.*)
  }

  .bss (NOLOAD) : ALIGN(16)
  {
    __bss_start = .;
    *(.bss .bss.* COMMON)
    __bss_end = .;
  }

  /DISCARD/ :
  {
    *(.comment)
    *(.note*)
    *(.eh_frame*)
    *(.riscv.attributes)
  }
}

