; -----------------------------------------------------------------------------
; amyzt_vram.asm - AMYZ tiny decompressor to TMS9918A VRAM (no table)
; The AMYZ stream with the fixed "log2" bucket table: bucket k has k extra
; bits and offset = 2^k + extra (k = 0..14, 15 = end), so offset is a 4-bit
; bit length k followed by the k bits below the implicit leading 1.
; HL = compressed stream, DE = destination VRAM address.
; Match bytes are copied VRAM -> VRAM one byte at a time (as zx0_vram.asm).
; RAM: stack only. Destroys AF, AF', BC, DE, HL. IX is not used.
; -----------------------------------------------------------------------------


amyzt_decompress:
        ld      bc, $ffff               ; last offset 1 (stored negated)
        push    bc
        ld      a, $80                  ; empty bit buffer (sentinel)

amyzt_literals:
        call    amyzt_elias               ; BC = run length
        ex      af, af'
        ld      a, e
        out     ($bf), a
        ld      a, d
        or      $40
        out     ($bf), a                ; VRAM write address
        ld      a, c
        ld      c, b
        or      a
        jr      z, amyzt_lit_pages
        inc     c
amyzt_lit_pages:
        ld      b, a
        ld      a, c
        ld      c, $be
amyzt_lit_loop:
        outi                            ; 16
        inc     de                      ; 6
        jr      nz, amyzt_lit_loop        ; 12 -> 34 T
        dec     a
        jr      nz, amyzt_lit_loop
        ex      af, af'
        call    amyzt_bit
        jr      c, amyzt_new_offset
        call    amyzt_elias               ; repeat last offset: BC = length

amyzt_copy:                               ; BC = length, [sp] = -offset
        ex      (sp), hl
        push    hl
        add     hl, de                  ; HL = VRAM source
        ex      af, af'
        set     6, d
amyzt_cpy_loop:
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
        jr      nz, amyzt_cpy_loop
        res     6, d
        ex      af, af'
        pop     hl
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        call    amyzt_bit
        jr      nc, amyzt_literals

amyzt_new_offset:
        pop     bc                      ; drop last offset
        ld      c, $10                  ; sentinel: 4 bits fall out through carry
amyzt_idx:
        call    amyzt_bit
        rl      c
        jr      nc, amyzt_idx           ; C = bucket index k
        inc     c
        bit     4, c
        ret     nz                      ; k = 15: end of stream
        push    de                      ; keep dest
        ld      b, c                    ; B = k + 1: djnz reads k bits
        ld      de, $fffe               ; complement of the implicit leading 1
        jr      amyzt_val_next
amyzt_val:
        call    amyzt_bit
        ccf
        rl      e
        rl      d                       ; DE = ~offset
amyzt_val_next:
        djnz    amyzt_val
        inc     de                      ; DE = -offset
        ex      de, hl
        ex      (sp), hl                ; [sp] = -offset, HL = dest
        ex      de, hl                  ; DE = dest, HL = stream
        call    amyzt_elias
        inc     bc                      ; length = gamma + 1
        jr      amyzt_copy

; carry = next bit
amyzt_bit:
        add     a, a
        ret     nz
        ld      a, (hl)
        inc     hl
        rla
        ret

; BC = interlaced Elias gamma value
amyzt_elias:
        ld      bc, 1
amyzt_elias_loop:
        call    amyzt_bit
        ret     c
        call    amyzt_bit
        rl      c
        rl      b
        jr      amyzt_elias_loop
