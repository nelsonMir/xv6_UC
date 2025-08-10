OUTPUT_ARCH(riscv)
ENTRY(main)

SECTIONS
{
  . = 0x1000;

  .text : {
    *(.text .text.*)
  }

  .rodata : {
    *(.rodata .rodata.*)
  }

  .data : {
    *(.data .data.*)
  }

  .bss : {
    *(.bss .bss.*)
  }

  /DISCARD/ : {
    *(.eh_frame)
    *(.eh_frame.*)
    *(.comment)
  }
}
