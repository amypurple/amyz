;-----------------------------------------------------------------------------
; warrior_z80.asm - AMYZ demo cartridge for MSX1 and SG-1000: decompresses the
; Warrior TMS9918A Graphics II bitmap (pattern 6144 B + colour 6144 B, AMYZ
; streams, 15-game corpus table) straight to VRAM with amyzf_vram, then shows it.
; Build (sjasmplus): -DMSX=1 for the MSX cartridge ($4000, "AB" header, VDP $98/$99),
; -DSG=1 for the SG-1000 cartridge ($0000, VDP $BE/$BF). Writes a 32 KB/16 KB .rom.
;-----------------------------------------------------------------------------
    DEVICE NONE
    IFDEF MSX
AMYZ_VDP_DATA equ $98
AMYZ_VDP_CTRL equ $99
    org $4000
    db "AB"
    dw main
    ds 12, 0
    ELSE
AMYZ_VDP_DATA equ $BE
AMYZ_VDP_CTRL equ $BF
    org $0000
    di
    ld sp, $c400
    jp main
    ds $38 - $, 0
    ei                          ; IRQ (not enabled by this demo)
    reti
    ds $66 - $, 0
    retn                        ; PAUSE button NMI
    ENDIF

main:
    di
    ld hl, vdpregs              ; Graphics II, display off while loading
    ld b, 8
    ld c, $80
.reg:
    ld a, (hl)
    out (AMYZ_VDP_CTRL), a
    ld a, c
    out (AMYZ_VDP_CTRL), a
    inc hl
    inc c
    djnz .reg
    ld de, $1800 | $4000        ; name table: 0..255 three times
    call setw
    ld b, 3
.nt:
    xor a
.ntl:
    out (AMYZ_VDP_DATA), a
    nop
    nop
    inc a
    jr nz, .ntl
    djnz .nt
    ld de, $1b00 | $4000        ; no sprites
    call setw
    ld a, $d0
    out (AMYZ_VDP_DATA), a
    ld hl, w_pat                ; the decompressions
    ld de, $0000
    call amyzf_decompress
    ld hl, w_col
    ld de, $2000
    call amyzf_decompress
    ld a, $c0                   ; display on
    out (AMYZ_VDP_CTRL), a
    ld a, $81
    out (AMYZ_VDP_CTRL), a
.stop:
    jr .stop

setw:                           ; VRAM write address DE (bit 14 set)
    ld a, e
    out (AMYZ_VDP_CTRL), a
    ld a, d
    out (AMYZ_VDP_CTRL), a
    ret

vdpregs:
    db $02, $80, $06, $ff, $03, $36, $07, $01

    include "../z80/amyzf_vram_port.asm"
amyzf_bases:
    incbin "warrior.tabzx"
w_pat:
    incbin "w_pat.amyz"
w_col:
    incbin "w_col.amyz"
end_of_code:
    IFDEF MSX
    ds $4000 + $4000 - $, $ff   ; 16 KB cartridge
    ELSE
    ds $4000 - $, $ff           ; 16 KB cartridge
    ENDIF
