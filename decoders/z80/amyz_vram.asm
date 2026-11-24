; -----------------------------------------------------------------------------
; amyz_vram.asm - AMYZ decompressor to TMS9918A VRAM
; The table is only the 15 bucket widths (15 bytes); the
; decoder computes the bases: base[0] = 1, base[j+1] = base[j] + 2^widths[j].
; HL = compressed stream, DE = destination VRAM address. The game defines the
; label amyz_widths: its 15-byte table, each byte = bucket width + 1.
; Match bytes are copied VRAM -> VRAM one byte at a time (as zx0_vram.asm).
; RAM: stack only. Destroys AF, AF', BC, DE, HL. IX is not used.
; -----------------------------------------------------------------------------

amyz_decompress:
        ld      bc, $ffff               ; last offset 1 (stored negated)
        push    bc
        ld      a, $80                  ; empty bit buffer (sentinel)

amyz_literals:
        call    amyz_elias               ; BC = run length
        ex      af, af'
        ld      a, e
        out     ($bf), a
        ld      a, d
        or      $40
        out     ($bf), a                ; VRAM write address
        ld      a, c
        ld      c, b
        or      a
        jr      z, amyz_lit_pages
        inc     c
amyz_lit_pages:
        ld      b, a
        ld      a, c
        ld      c, $be
amyz_lit_loop:
        outi                            ; 16
        inc     de                      ; 6
        jr      nz, amyz_lit_loop        ; 12 -> 34 T
        dec     a
        jr      nz, amyz_lit_loop
        ex      af, af'
        call    amyz_bit
        jr      c, amyz_new_offset
        call    amyz_elias               ; repeat last offset: BC = length

amyz_copy:                               ; BC = length, [sp] = -offset
        ex      (sp), hl
        push    hl
        add     hl, de                  ; HL = VRAM source
        ex      af, af'
        set     6, d
amyz_cpy_loop:
        push    bc
        ld      c, $bf
        out     (c), l
        out     (c), h                  ; read address
        inc     hl
        nop
        nop
        nop
        in      a, ($be)                ; 29 T after the address
        out     (c), e
        out     (c), d                  ; write address
        inc     de
        nop
        nop
        out     ($be), a                ; 25 T after the address
        pop     bc
        dec     bc
        ld      a, b
        or      c
        jr      nz, amyz_cpy_loop
        res     6, d
        ex      af, af'
        pop     hl
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        call    amyz_bit
        jr      nc, amyz_literals

amyz_new_offset:
        pop     bc                      ; drop last offset
        ld      c, $10                  ; sentinel: 4 bits fall out through carry
amyz_idx:
        call    amyz_bit
        rl      c
        jr      nc, amyz_idx            ; C = bucket index k
        inc     c
        bit     4, c
        ret     nz                      ; k = 15: end of stream
        push    de                      ; keep dest
        push    hl                      ; keep stream
        ld      hl, amyz_widths         ; 15 bytes: widths[j] + 1
        ld      de, $ffff               ; DE = -base[0] = -1
amyz_base:
        ld      b, (hl)                 ; B = widths[j] + 1
        inc     hl
        dec     c
        jr      z, amyz_base_done       ; j = k
        push    hl
        ld      hl, $ffff
        jr      amyz_pow_next
amyz_pow:
        add     hl, hl
amyz_pow_next:
        djnz    amyz_pow                ; HL = -2^widths[j]
        add     hl, de
        ex      de, hl                  ; DE = -base[j+1] = -(base[j] + 2^widths[j])
        pop     hl
        jr      amyz_base
amyz_base_done:
        pop     hl                      ; HL = stream
        push    de                      ; [sp] = -base[k]
        ld      de, $ffff
        jr      amyz_val_next
amyz_val:
        call    amyz_bit
        ccf
        rl      e
        rl      d                       ; DE = ~extra
amyz_val_next:
        djnz    amyz_val                ; widths[k] bits
        ex      (sp), hl                ; HL = -base, [sp] = stream
        add     hl, de
        inc     hl                      ; HL = -base - extra = -offset
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        pop     bc
        pop     de                      ; dest
        push    bc                      ; [sp] = -offset
        call    amyz_elias
        inc     bc                      ; length = gamma + 1
        jr      amyz_copy

; carry = next bit
amyz_bit:
        add     a, a
        ret     nz
        ld      a, (hl)
        inc     hl
        rla
        ret

; BC = interlaced Elias gamma value
amyz_elias:
        ld      bc, 1
amyz_elias_loop:
        call    amyz_bit
        ret     c
        call    amyz_bit
        rl      c
        rl      b
        jr      amyz_elias_loop
