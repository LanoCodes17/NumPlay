/* First step of a NumPlay module: `ld -r -d -T tools/module.ld game.o`.
   Merges a game into one code section, one .data and one .bss, and drops what
   the launcher does not need (the game's own name, icon and unwind tables). */
SECTIONS {
  /DISCARD/ : {
    *(.rodata.eadk_app_name) *(.rodata.eadk_app_icon) *(.rodata.eadk_api_level)
    *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) *(.debug*)
  }
  .text : {
    *(.text .text.* .rodata .rodata.* .glue_7 .glue_7t .vfp11_veneer .v4_bx)
  }
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.* COMMON) }
}
