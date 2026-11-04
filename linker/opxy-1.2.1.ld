/* Module layout for stock OP-XY 1.2.1: the 2 MiB tail of the application
 * heap, [0x8d2d0380, 0x8d4d0380). Its last 16 KiB hold the relocated stock
 * effect records, engine records and screen slots (tooling/stock_tables.py). */
OUTPUT_ARCH(bfin)
ENTRY(extensions_synth_exit)
MEMORY { MODULE (rwx) : ORIGIN = 0x8d2d0380, LENGTH = 0x1fc000 }
SECTIONS {
  __extensions_reserved_start = ORIGIN(MODULE);
  __extensions_reserved_end = ORIGIN(MODULE) + LENGTH(MODULE);
  __stock_effect_records = 0x8d4cc380;
  __stock_engine_records = 0x8d4ccf50;
  __stock_screen_slots = 0x8d4cff50;
  .text : { *(.text .text.*) } > MODULE
  .rodata : ALIGN(4) { *(.rodata .rodata.* .data.rel.ro .data.rel.ro.*) } > MODULE
  .data : ALIGN(4) { *(.data .data.* .sdata .sdata.*) } > MODULE
  __extensions_initialized_end = .;
  .bss (NOLOAD) : ALIGN(4) {
    __extensions_bss_start = .;
    *(.bss .bss.* .sbss .sbss.* COMMON)
    __extensions_bss_end = .;
  } > MODULE
  __extensions_module_end = .;
  .init_array : { *(.init_array .init_array.* .ctors .ctors.*) }
  .fini_array : { *(.fini_array .fini_array.* .dtors .dtors.*) }
  /DISCARD/ : { *(.comment .note* .eh_frame .eh_frame_hdr .gcc_except_table) }
  ASSERT(SIZEOF(.init_array) == 0, "Static constructors are not run by the stock loader")
  ASSERT(SIZEOF(.fini_array) == 0, "Static destructors are not supported")
  ASSERT(__extensions_reserved_end == __stock_effect_records, "Module must end where the relocated tables begin")
  ASSERT(__stock_effect_records + 63 * 48 == __stock_engine_records, "Effect records overlap engine records")
  ASSERT(__stock_engine_records + 256 * 48 == __stock_screen_slots, "Engine records overlap screen slots")
  ASSERT(__stock_screen_slots + 256 * 4 <= 0x8d4d0380, "Relocated tables exceed the reservation")
  ASSERT(__extensions_module_end <= __extensions_reserved_end, "Module overlaps the relocated tables")
}
