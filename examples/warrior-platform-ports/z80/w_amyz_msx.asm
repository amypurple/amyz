    DEVICE NONE
AMYZ_VDP_DATA equ $98
AMYZ_VDP_CTRL equ $99
    org $8000
    jp amyz_decompress
    include "amyz_vram_port.asm"
amyz_widths equ $BF00
