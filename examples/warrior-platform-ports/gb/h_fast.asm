DEF amyz_hoff EQU $FF80
DEF amyz_hbits EQU $FF82
SECTION "dec", ROM0[$0200]
        INCLUDE "amyzf_gb.asm"
SECTION "tab", ROM0[$3F00]
amyz_bases:: ds 45
