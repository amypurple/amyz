; -----------------------------------------------------------------------------
; amyz_ram.asm - AMYZ decompressor, RAM to RAM (small version, "amyz ram")
; For systems with RAM: MSX, ZX Spectrum, Amstrad CPC, SG-1000, ColecoVision
; with the Super Game Module. Same stream as amyz_vram.asm, 15-byte table.
; HL = compressed stream, DE = destination (RAM). The game defines the label
; amyz_widths: its 15-byte table, each byte = bucket width + 1. The decoder
; computes base[k] = 1 + sum(2^widths[j], j < k) for every new offset.
; Literals and matches are copied with LDIR. Not for in-place decoding.
; RAM: stack only. Destroys AF, BC, DE, HL. On return DE = end of output.
; -----------------------------------------------------------------------------

amyzr_decompress:
        ld      bc, $ffff               ; last offset 1 (stored negated)
        push    bc
        ld      a, $80                  ; empty bit buffer (sentinel)

amyzr_literals:
        call    amyzr_elias             ; BC = run length
        ldir
        call    amyzr_bit
        jr      c, amyzr_new_offset
        call    amyzr_elias             ; repeat last offset: BC = length

amyzr_copy:                             ; BC = length, [sp] = -offset
        ex      (sp), hl
        push    hl
        add     hl, de                  ; HL = source = dest - offset
        ldir
        pop     hl
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        call    amyzr_bit
        jr      nc, amyzr_literals

amyzr_new_offset:
        pop     bc                      ; drop last offset
        ld      c, $10                  ; sentinel: 4 bits fall out through carry
amyzr_idx:
        call    amyzr_bit
        rl      c
        jr      nc, amyzr_idx           ; C = bucket index k
        inc     c
        bit     4, c
        ret     nz                      ; k = 15: end of stream
        push    de                      ; keep dest
        push    hl                      ; keep stream
        ld      hl, amyz_widths         ; 15 bytes: widths[j] + 1
        ld      de, $ffff               ; DE = -base[0] = -1
amyzr_base:
        ld      b, (hl)                 ; B = widths[j] + 1
        inc     hl
        dec     c
        jr      z, amyzr_base_done      ; j = k
        push    hl
        ld      hl, $ffff
        jr      amyzr_pow_next
amyzr_pow:
        add     hl, hl
amyzr_pow_next:
        djnz    amyzr_pow               ; HL = -2^widths[j]
        add     hl, de
        ex      de, hl                  ; DE = -base[j+1]
        pop     hl
        jr      amyzr_base
amyzr_base_done:
        pop     hl                      ; HL = stream
        push    de                      ; [sp] = -base[k]
        ld      de, $ffff
        jr      amyzr_val_next
amyzr_val:
        call    amyzr_bit
        ccf
        rl      e
        rl      d                       ; DE = ~extra
amyzr_val_next:
        djnz    amyzr_val               ; widths[k] bits
        ex      (sp), hl                ; HL = -base, [sp] = stream
        add     hl, de
        inc     hl                      ; HL = -base - extra = -offset
        ex      (sp), hl                ; HL = stream, [sp] = -offset
        pop     bc
        pop     de                      ; dest
        push    bc                      ; [sp] = -offset
        call    amyzr_elias
        inc     bc                      ; length = gamma + 1
        jr      amyzr_copy

; carry = next bit
amyzr_bit:
        add     a, a
        ret     nz
        ld      a, (hl)
        inc     hl
        rla
        ret

; BC = interlaced Elias gamma value
amyzr_elias:
        ld      bc, 1
amyzr_elias_loop:
        call    amyzr_bit
        ret     c
        call    amyzr_bit
        rl      c
        rl      b
        jr      amyzr_elias_loop
