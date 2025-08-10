OUTPUT_ARCH("riscv")
ENTRY(start)

SECTIONS
{
  . = 0;

  .text : {
    *(.text)
  }

  .data : {
    *(.data)
  }

  .rodata : {
    *(.rodata)
  }

  .bss : {
    *(.bss COMMON)
  }
}
