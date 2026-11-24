; warrior_gb.asm - Game Boy demo: decode the Warrior picture (4 greys, AMYZ)
; straight into VRAM with the LCD off, then turn the LCD on and show it.
; Build with -DSMALL=1 for the small decoder (15-byte table), default fast.
; Licence: CC0 (the picture data is Amy's and is not covered by this).

DEF rLY   EQU $FF44
DEF rLCDC EQU $FF40
DEF rSCY  EQU $FF42
DEF rSCX  EQU $FF43
DEF rBGP  EQU $FF47
DEF rIE   EQU $FFFF
DEF amyz_hoff  EQU $FF80                ; 2 bytes of HRAM
DEF amyz_hbits EQU $FF82                ; 1 byte (fast version)
IF !DEF(SMALL)
DEF SMALL EQU 0
ENDC

SECTION "entry", ROM0[$0100]
        nop
        jp      start
        ds      $150 - @, 0             ; header, filled in by rgbfix

SECTION "main", ROM0[$0150]
start:
        di
        ld      sp, $fffe
        xor     a
        ldh     [rIE], a
.wait_vbl:                              ; the LCD may only be switched off in VBlank
        ldh     a, [rLY]
        cp      144
        jr      c, .wait_vbl
        xor     a
        ldh     [rLCDC], a              ; LCD off: VRAM is free to write
        ld      hl, tiles_amyz
        ld      de, $8000
        call    decode
        ld      hl, map_amyz
        ld      de, $9800
        call    decode
        xor     a
        ldh     [rSCX], a
        ldh     [rSCY], a
        ld      a, %11100100            ; shade n -> grey n
        ldh     [rBGP], a
        ld      a, %10010001            ; LCD on, tiles at $8000, map $9800, BG on
        ldh     [rLCDC], a
.forever:
        jr      .forever

decode:
IF SMALL
        jp      amyz_decompress
        INCLUDE "amyz_gb.asm"
amyz_widths:
        INCBIN  "data/gb.tabz"
ELSE
        jp      amyzf_decompress
        INCLUDE "amyzf_gb.asm"
amyz_bases:
        INCBIN  "data/gb.tabzx"
ENDC

tiles_amyz: INCBIN "data/tiles.amyz"
map_amyz:   INCBIN "data/map.amyz"
