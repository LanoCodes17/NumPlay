/* data.bin (tools/pack.py) in the app's read-only data */
  .section .rodata.hk_data,"a"
  .balign 4
  .global hk_data
hk_data:
  .incbin DATA_BIN
  .balign 4
#ifdef __ELF__
  .section .note.GNU-stack,"",%progbits
#endif
