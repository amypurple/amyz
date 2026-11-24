; -----------------------------------------------------------------------------
; amyzf_vram.asm - AMYZ decompressor to TMS9918A VRAM, fast version ("amyz fast")
; Same stream as amyz_vram.asm. The table is the EXPANDED table (45 bytes), made
; by the build tool from the 15-byte width table (expandTable in amyz.js,
; expand_table in amyz.py). Entry k (k = 0..14), 3 bytes:
;     +0, +1 : (1 - base[k]) mod 65536, little endian
;     +2     : widths[k]
; HL = compressed stream, DE = destination VRAM address. The game defines the
; label amyzf_bases (its 45-byte expanded table, anywhere in ROM).
; Match bytes are copied VRAM -> VRAM one byte at a time, counted with djnz.
; RAM: stack only. Destroys AF, AF', BC, DE, HL. IX and IY are not used.
; -----------------------------------------------------------------------------

amyzf_decompress:
        ld      bc, $ffff               ; last offset 1 (stored negated)
        push    bc
        ld      a, $80                  ; empty bit buffer (sentinel)

amyzf_literals:
        call    amyzf_elias             ; BC = run length
        ex      af, af'
        ld      a, e
        out     ($bf), a
        ld      a, d
        or      $40
        out     ($bf), a                ; VRAM write address
        ld      a, c
        ld      c, b
        or      a
        jr      z, amyzf_lit_pages
        inc     c
amyzf_lit_pages:
        ld      b, a
        ld      a, c
        ld      c, $be
amyzf_lit_loop:
        outi                            ; 16
        inc     de                      ; 6
        jr      nz, amyzf_lit_loop      ; 12 -> 34 T
        dec     a
        jr      nz, amyzf_lit_loop
        ex      af, af'
        add     a, a                    ; next bit
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        jr      c, amyzf_new_offset
        call    amyzf_elias             ; repeat last offset: BC = length

amyzf_copy:                             ; BC = length, [sp] = -offset
        ex      (sp), hl
        push    hl
        add     hl, de                  ; HL = VRAM source
        ex      af, af'
        set     6, d
        ld      a, c                    ; split BC: B = partial page, pages on the stack
        ld      c, b
        or      a
        jr      z, amyzf_cpy_pages
        inc     c
amyzf_cpy_pages:
        ld      b, a
        ld      a, c
        push    af                      ; page count
        ld      c, $bf
amyzf_cpy_loop:
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
        djnz    amyzf_cpy_loop          ; 115 T per byte
        pop     af
        dec     a
        jr      z, amyzf_cpy_end
        push    af
        jr      amyzf_cpy_loop
amyzf_cpy_end:
        res     6, d
        ex      af, af'
        pop     hl
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        add     a, a                    ; next bit
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        jr      nc, amyzf_literals

amyzf_new_offset:
        pop     bc                      ; drop last offset
        push    de                      ; keep dest
        ld      bc, $0400
amyzf_idx:
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        rl      c
        djnz    amyzf_idx               ; C = bucket index k
        ld      b, a                    ; B = bit buffer
        ld      a, c
        cp      15
        jr      z, amyzf_end
        push    hl                      ; keep stream
        add     a, a
        add     a, c                    ; A = 3k
        ld      hl, amyzf_bases
        add     a, l
        ld      l, a
        adc     a, h
        sub     l
        ld      h, a                    ; HL = entry k
        ld      e, (hl)
        inc     hl
        ld      d, (hl)                 ; DE = 1 - base
        inc     hl
        ld      a, b                    ; A = bit buffer
        ld      b, (hl)                 ; B = width
        pop     hl                      ; HL = stream
        push    de                      ; [sp] = 1 - base
        ld      de, $ffff
        inc     b
        dec     b
        jr      z, amyzf_val_done
amyzf_val:
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        ccf
        rl      e
        rl      d
        djnz    amyzf_val               ; DE = ~extra
amyzf_val_done:
        ex      (sp), hl                ; HL = 1 - base, [sp] = stream
        add     hl, de                  ; HL = 1 - base - extra - 1 = -offset
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        pop     bc
        pop     de                      ; dest
        push    bc                      ; [sp] = -offset
        call    amyzf_elias
        inc     bc                      ; length = gamma + 1
        jp      amyzf_copy

amyzf_end:
        pop     de
        ret

; BC = interlaced Elias gamma value
amyzf_elias:
        ld      bc, 1
amyzf_elias_loop:
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        ret     c
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        rl      c
        rl      b
        jr      amyzf_elias_loop
