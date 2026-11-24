; -----------------------------------------------------------------------------
; amyzf_gb.asm - AMYZ decompressor for the Game Boy (SM83), fast version.
; Same stream as amyz_gb.asm, but uses the 45-byte expanded .tabzx table
; (entry k = (1 - base[k]) lo, hi, width[k]), inlined bit reads and gamma,
; and a page-split copy loop.
; In:  HL = stream, DE = destination (RAM, or VRAM with the LCD off).
;      The program defines amyz_bases (45 bytes) and reserves 3 bytes of HRAM
;      at amyz_hoff (2, negated offset) and amyz_hbits (1).
; Out: DE = end of output. Destroys AF, BC, DE, HL. Stack: 4 bytes.
; Licence: CC0.
; -----------------------------------------------------------------------------

; carry = next bit (A = bit buffer, HL = stream)
MACRO AMYZF_BIT
        add     a
        jr      nz, .b\@
        ld      a, [hl+]
        rla
.b\@:
ENDM

; BC = interlaced Elias gamma value
MACRO AMYZF_GAMMA
        ld      bc, 1
.g\@:
        AMYZF_BIT
        jr      c, .d\@
        AMYZF_BIT
        rl      c
        rl      b
        jr      .g\@
.d\@:
ENDM

; copy BC (>= 1) bytes from HL to DE, destroys A
MACRO AMYZF_COPY
        ld      a, c
        and     a
        jr      z, .c\@
        inc     b
.c\@:
        ld      a, [hl+]
        ld      [de], a
        inc     de
        dec     c
        jr      nz, .c\@
        dec     b
        jr      nz, .c\@
ENDM

amyzf_decompress::
        ld      a, $ff
        ldh     [amyz_hoff], a
        ldh     [amyz_hoff+1], a
        ld      a, $80
.literals:
        AMYZF_GAMMA
        ldh     [amyz_hbits], a
        AMYZF_COPY
        ldh     a, [amyz_hbits]
        AMYZF_BIT
        jr      c, .new_offset
        AMYZF_GAMMA
.match:
        ldh     [amyz_hbits], a
        push    hl
        ldh     a, [amyz_hoff]
        add     e
        ld      l, a
        ldh     a, [amyz_hoff+1]
        adc     d
        ld      h, a                    ; HL = dest - offset
        AMYZF_COPY
        pop     hl
        ldh     a, [amyz_hbits]
        AMYZF_BIT
        jp      nc, .literals
.new_offset:
        ld      c, $10
.idx:
        AMYZF_BIT
        rl      c
        jr      nc, .idx                ; C = k
        ld      b, a
        ld      a, c
        cp      15
        ret     z
        push    de
        push    hl
        add     a
        add     c
        add     LOW(amyz_bases)
        ld      l, a
        adc     HIGH(amyz_bases)
        sub     l
        ld      h, a
        ld      a, [hl+]
        ldh     [amyz_hoff], a
        ld      a, [hl+]
        ldh     [amyz_hoff+1], a
        ld      c, [hl]
        inc     c
        pop     hl
        ld      a, b
        ld      de, $ffff
        jr      .val_next
.val:
        AMYZF_BIT
        ccf
        rl      e
        rl      d                       ; DE = ~extra
.val_next:
        dec     c
        jr      nz, .val
        ld      b, a
        ldh     a, [amyz_hoff]
        add     e
        ldh     [amyz_hoff], a
        ldh     a, [amyz_hoff+1]
        adc     d
        ldh     [amyz_hoff+1], a        ; -offset = (1 - base) + ~extra
        ld      a, b
        pop     de
        AMYZF_GAMMA
        inc     bc
        jp      .match
