    DEVICE NONE
AMYZ_VDP_DATA equ $BE
AMYZ_VDP_CTRL equ $BF
    org $8000
    jp amyz_decompress
    include "amyz_vram_port.asm"
amyz_widths equ $BF00
