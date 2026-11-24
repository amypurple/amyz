DEF amyz_hoff EQU $FF80
SECTION "dec", ROM0[$0200]
        INCLUDE "amyz_gb.asm"
SECTION "tab", ROM0[$3F00]
amyz_widths:: ds 15
