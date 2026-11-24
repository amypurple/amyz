    DEVICE NONE
AMYZ_VDP_DATA equ $98
AMYZ_VDP_CTRL equ $99
    org $8000
    jp amyzf_decompress
    include "amyzf_vram_port.asm"
amyzf_bases equ $BF00
