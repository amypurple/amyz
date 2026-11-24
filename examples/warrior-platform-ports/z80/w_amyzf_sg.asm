    DEVICE NONE
AMYZ_VDP_DATA equ $BE
AMYZ_VDP_CTRL equ $BF
    org $8000
    jp amyzf_decompress
    include "amyzf_vram_port.asm"
amyzf_bases equ $BF00
