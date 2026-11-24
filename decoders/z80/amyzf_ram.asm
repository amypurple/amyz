; -----------------------------------------------------------------------------
; amyzf_ram.asm - AMYZ decompressor, RAM to RAM, fast version ("amyz ram fast")
; For systems with RAM: MSX, ZX Spectrum, Amstrad CPC, SG-1000, ColecoVision
; with the Super Game Module. Same stream as amyz_vram.asm.
; HL = compressed stream, DE = destination (RAM). The game defines the label
; amyzf_bases: the 45-byte EXPANDED table (see amyzf_vram.asm / SPEC.md):
; entry k = (1 - base[k]) lo, hi, widths[k].
; Literals and matches are copied with LDIR; bit reads are inlined.
; Not for in-place decoding. RAM: stack only. Destroys AF, BC, DE, HL.
; -----------------------------------------------------------------------------

amyzfr_decompress:
        ld      bc, $ffff               ; last offset 1 (stored negated)
        push    bc
        ld      a, $80                  ; empty bit buffer (sentinel)

amyzfr_literals:
        call    amyzfr_elias            ; BC = run length
        ldir
        add     a, a                    ; next bit
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        jr      c, amyzfr_new_offset
        call    amyzfr_elias            ; repeat last offset: BC = length

amyzfr_copy:                            ; BC = length, [sp] = -offset
        ex      (sp), hl
        push    hl
        add     hl, de                  ; HL = source = dest - offset
        ldir
        pop     hl
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        add     a, a                    ; next bit
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        jr      nc, amyzfr_literals

amyzfr_new_offset:
        pop     bc                      ; drop last offset
        push    de                      ; keep dest
        ld      bc, $0400
amyzfr_idx:
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        rl      c
        djnz    amyzfr_idx              ; C = bucket index k
        ld      b, a                    ; B = bit buffer
        ld      a, c
        cp      15
        jr      z, amyzfr_end
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
        jr      z, amyzfr_val_done
amyzfr_val:
        add     a, a
        jr      nz, $+5
        ld      a, (hl)
        inc     hl
        rla
        ccf
        rl      e
        rl      d
        djnz    amyzfr_val              ; DE = ~extra
amyzfr_val_done:
        ex      (sp), hl                ; HL = 1 - base, [sp] = stream
        add     hl, de                  ; HL = -offset
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        pop     bc
        pop     de                      ; dest
        push    bc                      ; [sp] = -offset
        call    amyzfr_elias
        inc     bc                      ; length = gamma + 1
        jp      amyzfr_copy

amyzfr_end:
        pop     de                      ; DE = end of output
        ret

; BC = interlaced Elias gamma value
amyzfr_elias:
        ld      bc, 1
amyzfr_elias_loop:
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
        jr      amyzfr_elias_loop
