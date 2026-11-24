; -----------------------------------------------------------------------------
; amyz_gb.asm - AMYZ decompressor for the Game Boy (Sharp SM83 / LR35902)
; Small version. Same stream as the Z80 amyz_ram.asm, 15-byte .tabz table.
; In:  HL = compressed stream, DE = destination (WRAM, HRAM-free RAM, or VRAM
;      while the LCD is off). The program defines amyz_widths (15 bytes, each
;      = bucket width + 1) and reserves 2 bytes of HRAM at amyz_hoff.
; Out: DE = end of output. Destroys AF, BC, DE, HL. Stack: 8 bytes.
; Not for in-place decoding. Licence: CC0.
; -----------------------------------------------------------------------------

amyz_decompress::
        ld      a, $ff                  ; last offset = 1, stored negated
        ldh     [amyz_hoff], a
        ldh     [amyz_hoff+1], a
        ld      a, $80                  ; A = bit buffer, empty (sentinel)
.literals:
        call    amyz_elias              ; BC = run length
        push    af
        call    amyz_copy
        pop     af
        call    amyz_bit
        jr      c, .new_offset
        call    amyz_elias              ; repeat last offset
.match:                                 ; BC = length
        push    hl
        push    af
        ldh     a, [amyz_hoff]
        add     e
        ld      l, a
        ldh     a, [amyz_hoff+1]
        adc     d
        ld      h, a                    ; HL = dest - offset
        call    amyz_copy
        pop     af
        pop     hl
        call    amyz_bit
        jr      nc, .literals
.new_offset:
        ld      c, $10                  ; 4 index bits, sentinel falls into carry
.idx:
        call    amyz_bit
        rl      c
        jr      nc, .idx
        inc     c
        bit     4, c
        ret     nz                      ; k = 15: end of stream
        push    de
        push    hl
        push    af
        ld      hl, amyz_widths
        ld      de, $ffff               ; DE = -base[0]
.base:
        ld      a, [hl+]
        ld      b, a                    ; B = width[j] + 1
        dec     c
        jr      z, .base_done
        push    hl
        ld      hl, $ffff
        jr      .pow_next
.pow:
        add     hl, hl
.pow_next:
        dec     b
        jr      nz, .pow                ; HL = -2^width[j]
        add     hl, de
        ld      d, h
        ld      e, l                    ; DE = -base[j+1]
        pop     hl
        jr      .base
.base_done:
        ld      a, e
        ldh     [amyz_hoff], a
        ld      a, d
        ldh     [amyz_hoff+1], a        ; hoff = -base[k]
        pop     af
        pop     hl
        ld      de, $ffff
        jr      .val_next
.val:
        call    amyz_bit
        ccf
        rl      e
        rl      d                       ; DE = ~extra
.val_next:
        dec     b
        jr      nz, .val
        push    af
        ldh     a, [amyz_hoff]
        scf
        adc     e
        ldh     [amyz_hoff], a
        ldh     a, [amyz_hoff+1]
        adc     d
        ldh     [amyz_hoff+1], a        ; hoff = -base - extra = -offset
        pop     af
        pop     de
        call    amyz_elias
        inc     bc                      ; length = gamma + 1
        jr      .match

; copy BC (>= 1) bytes from HL to DE; destroys A
amyz_copy:
        ld      a, [hl+]
        ld      [de], a
        inc     de
        dec     bc
        ld      a, b
        or      c
        jr      nz, amyz_copy
        ret

; carry = next bit
amyz_bit:
        add     a
        ret     nz
        ld      a, [hl+]
        rla
        ret

; BC = interlaced Elias gamma value
amyz_elias:
        ld      bc, 1
.loop:
        call    amyz_bit
        ret     c
        call    amyz_bit
        rl      c
        rl      b
        jr      .loop
