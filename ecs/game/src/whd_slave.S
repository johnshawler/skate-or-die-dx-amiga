/* WHDLoad slave of Skate or Die (Amiga port): header (slave version 17) and the loader, position independent.
   WHDLoad calls the loader with a0 = resload. It loads data/SkateOrDie.bin (image made by tools/elf2whd.py) into the expansion memory, whose address
   WHDLoad has put into ws_ExpMem, applies the relocations and calls the image's first instruction (see `start` in ow.c) with
   a0 = resload, a1 = first free byte after the image, d0 = bytes still free in the expansion memory, d1 = size of the base memory.
   When the program returns, the loader hands control back to WHDLoad (resload_Abort, TDREASON_OK). */
        .set    BASEMEM, 0x180000        /* Chip RAM the game may use */
        .set    EXPMEM,  0x200000        /* expansion (Fast) memory it needs */
        .text
        .globl  _start
_start:
        moveq   #-1,%d0                  /* ws_Security */
        rts
        .ascii  "WHDLOADS"               /* ws_ID */
        .word   17                       /* ws_Version */
        .word   0x72                     /* ws_flags: NoError | Req68020 | ReqAGA | NoKbd */
        .long   BASEMEM                  /* ws_BaseMemSize */
        .long   0                        /* ws_ExecInstall */
        .word   loader-_start            /* ws_GameLoader */
        .word   dir-_start               /* ws_CurrentDir */
        .word   0                        /* ws_DontCache */
        .byte   0                        /* ws_keydebug */
        .byte   0x59                     /* ws_keyexit: F10 */
ws_expmem:
        .long   EXPMEM                   /* ws_ExpMem (WHDLoad replaces the size by the address) */
        .word   name-_start              /* ws_name */
        .word   copy-_start              /* ws_copy */
        .word   info-_start              /* ws_info */
        .word   0                        /* ws_kickname */
        .long   0                        /* ws_kicksize */
        .word   0                        /* ws_kickcrc */
        .word   0                        /* ws_config */

loader:
        move.l  %a0,%a2                  /* a2 = resload */
        move.l  ws_expmem(%pc),%a3       /* a3 = start of the expansion memory */
        lea     progname(%pc),%a0
        move.l  %a3,%a1
        jsr     8(%a2)                   /* resload_LoadFile */
        tst.l   %d0
        beq.s   fail
        cmp.l   #0x534F4457,(%a3)        /* 'SODW' */
        bne.s   fail
        move.l  %d0,%d5                  /* file length */
        lea     16(%a3),%a4              /* a4 = the image */
        move.l  4(%a3),%d3               /* image length */
        move.l  8(%a3),%d4               /* number of relocations */
        lea     (%a4,%d3.l),%a5          /* the table */
        move.l  %a4,%d6
        bra.s   2f
1:      move.l  (%a5)+,%d2
        add.l   %d6,(%a4,%d2.l)
2:      subq.l  #1,%d4
        bpl.s   1b
        jsr     0x20(%a2)                /* resload_FlushCache */
        move.l  %d5,%d0
        addq.l  #7,%d0
        and.l   #-8,%d0                  /* file length rounded up: what the image and its tables occupy */
        lea     (%a3,%d0.l),%a1          /* first free byte */
        move.l  #EXPMEM,%d1
        sub.l   %d0,%d1
        move.l  %d1,%d0                  /* bytes left */
        move.l  #BASEMEM,%d1
        move.l  %a2,%a0
        jsr     (%a4)                    /* the program */
        pea     -1.w                     /* TDREASON_OK */
        jmp     4(%a2)                   /* resload_Abort */
fail:   pea     progname(%pc)            /* secondary: the file name */
        clr.l   -(%sp)
        pea     1.w                      /* TDREASON_DOSREAD */
        jmp     4(%a2)

progname: .asciz "SkateOrDie.bin"
dir:    .asciz  "data"
name:   .asciz  "Skate or Die DX"
copy:   .asciz  "(c)1988 Electronic Arts"
info:   .ascii  "DX Amiga version by Boatofcar\n"
        .ascii  "Needs AGA, 68020+, 2 MB Chip and 4 MB Fast RAM\n"
        .ascii  "Joystick or cursor keys + SPACE/RETURN, ESC quits\n"
        .asciz  "Version 1.0 (WHDLoad)"
