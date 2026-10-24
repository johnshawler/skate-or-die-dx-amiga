ENTRY(start)
SECTIONS
{
    . = 0;
    .text : {
        *(.text.start)
        *(.text .text.*)
        *(.rodata .rodata.*)
        *(.data .data.*)
        *(.bss .bss.* COMMON)       /* zero-initialised data is stored in the file (it is small): the hunk has no BSS */
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
