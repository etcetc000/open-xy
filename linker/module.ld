/* Module file (.opx): one contiguous image linked at MODULE_BASE (set by
 * tooling/build_module.py), bss after it. The builder links twice at different
 * bases and checks its relocation list against the second link. */
OUTPUT_FORMAT("elf32-bfin")
ENTRY(_opxy_module_init)
SECTIONS
{
  . = MODULE_BASE;
  .text : { *(.text .text.*) }
  .rodata ALIGN(4) : { *(.rodata .rodata.* .constdata .constdata.* .data.rel.ro .data.rel.ro.*) }
  .data ALIGN(4) : { *(.data .data.* .sdata .sdata.*) . = ALIGN(4); }
  .bss ALIGN(4) (NOLOAD) : { *(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(4); }
  .init_array : { *(.init_array .init_array.* .ctors .ctors.*) }
  .fini_array : { *(.fini_array .fini_array.* .dtors .dtors.*) }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) *(.gcc_except_table) }
  ASSERT(SIZEOF(.init_array) == 0, "Static constructors are not run by the module loader")
  ASSERT(SIZEOF(.fini_array) == 0, "Static destructors are not supported")
}
