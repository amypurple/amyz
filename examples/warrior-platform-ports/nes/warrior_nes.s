; -----------------------------------------------------------------------------
; warrior_nes.s - AMYZ demo for the NES: palettes
; fitted by optimisation, picture aligned to the attribute grid, face sprites.
; NROM-128, 8 KB CHR-RAM: decompresses the adapted Warrior picture (data/chr.amyz -> PPU $0000, 2 x 256 tiles;
; data/nt.amyz -> PPU $2000, nametable + attributes) with amyz_nes.s, then shows
; it. The top half uses pattern table 0, the bottom half table 1: the background
; table is switched mid-frame when the sprite-0 hit flag rises (sprite 0 is the
; background tile under it, drawn behind the background, so it stays invisible).
; Order: palettes, CHR-RAM and nametable decoding, OAM, all with rendering off;
; rendering is turned on last, in vblank.
; ca65 syntax; build with build_nes.sh.
; -----------------------------------------------------------------------------
        .import amyz_decompress
        .importzp amyz_src, amyz_dst
        .export amyz_widths

        .segment "HEADER"
        .byte   "NES", $1a, 1, 0, $00, $00, 0, 0, 0, 0, 0, 0, 0, 0   ; 16 KB PRG, CHR-RAM, mapper 0

        .segment "CODE"
reset:  sei
        cld
        ldx     #$40
        stx     $4017
        ldx     #$ff
        txs
        inx
        stx     $2000                  ; NMI off
        stx     $2001                  ; rendering off
        stx     $4010
        bit     $2002
:       bit     $2002                  ; first vblank
        bpl     :-
        ldx     #0                      ; OAM page: sprite 0 (split marker) + face sprites
:       lda     oam,x
        sta     $0200,x
        inx
        bne     :-
:       bit     $2002                  ; second vblank: the PPU is ready
        bpl     :-
        lda     #$3f                   ; palettes
        sta     $2006
        lda     #$00
        sta     $2006
        ldx     #0                      ; 4 background + 4 sprite palettes
:       lda     pal,x
        sta     $2007
        inx
        cpx     #32
        bne     :-
        lda     #<chr_z                 ; CHR-RAM, both pattern tables
        sta     amyz_src
        lda     #>chr_z
        sta     amyz_src+1
        lda     #$00
        sta     amyz_dst
        sta     amyz_dst+1
        jsr     amyz_decompress
        lda     #<nt_z                  ; nametable 0 + attributes
        sta     amyz_src
        lda     #>nt_z
        sta     amyz_src+1
        lda     #$00
        sta     amyz_dst
        lda     #$20
        sta     amyz_dst+1
        jsr     amyz_decompress
:       bit     $2002                  ; turn rendering on in vblank
        bpl     :-
        lda     #0
        sta     $2003
        lda     #$02
        sta     $4014                  ; OAM DMA
        lda     #0
        sta     $2005
        sta     $2005
        lda     #$80                   ; NMI on, BG table 0, sprites table 0
        sta     $2000
        lda     #%00011110              ; show BG and sprites, left columns too
        sta     $2001
main:
:       bit     $2002                  ; sprite-0 flag clear (vblank)...
        bvs     :-
:       bit     $2002                  ; ...then set at the split line
        bvc     :-
        lda     #$90                   ; BG pattern table 1 for the bottom half
        sta     $2000
        jmp     main

nmi:    pha
        lda     #0
        sta     $2003
        lda     #$02
        sta     $4014
        lda     #$80                   ; BG table 0 for the top half
        sta     $2000
        lda     #0
        sta     $2005
        sta     $2005
        pla
irq:    rti

        .segment "RODATA"
pal:    .incbin "data/pal.bin"
oam:    .incbin "data/oam.bin"
amyz_widths:
        .incbin "data/nes.tabz"
chr_z:  .incbin "data/chr.amyz"
nt_z:   .incbin "data/nt.amyz"

        .segment "VECTORS"
        .word   nmi, reset, irq
