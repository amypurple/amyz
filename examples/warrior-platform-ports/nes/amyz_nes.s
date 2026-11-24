; -----------------------------------------------------------------------------
; amyz_nes.s - AMYZ decompressor for the NES: from CPU memory straight into PPU
; memory through $2006/$2007 (the 6502 counterpart of the Z80 VRAM decoders).
; Derived from amyz_6502.s (small version); same streams, same 15-byte table at
; amyz_widths. Back-references are read back from the PPU ($2007 has a one-byte
; read buffer: the first read after setting the address is discarded).
; Call with rendering off (or in vblank), PPUCTRL increment = 1 (bit 2 clear),
; amyz_src = stream (CPU), amyz_dst = PPU address ($0000-$3eff), jsr amyz_decompress.
; Zero page: 11 bytes at AMYZ_ZP. Destroys A, X, Y. No RAM tables. ca65 syntax.
; -----------------------------------------------------------------------------

.ifndef PPUADDR
PPUSTATUS = $2002
PPUADDR   = $2006
PPUDATA   = $2007
.endif

.ifndef AMYZ_ZP
AMYZ_ZP = $f0                   ; relocatable: assemble with -D AMYZ_ZP=$xx
.endif

amyz_src  = AMYZ_ZP+0           ; 2: stream pointer
amyz_dst  = AMYZ_ZP+2           ; 2: output pointer
amyz_cpy  = AMYZ_ZP+4           ; 2: copy source / base accumulator
amyz_len  = AMYZ_ZP+6           ; 2: length / bucket index
amyz_off  = AMYZ_ZP+8           ; 2: last offset, negated / scratch
amyz_bits = AMYZ_ZP+10          ; 1: bit buffer (sentinel bit)

        .export amyz_decompress
        .import amyz_widths
        .ifndef AMYZ_NOEXPORTZP         ; define when linking several decoders
        .exportzp amyz_src, amyz_dst
        .endif

        .segment "CODE"

amyz_decompress:
        bit     PPUSTATUS               ; reset the PPU address latch
        lda     #$ff                    ; last offset 1, stored as -1
        sta     amyz_off
        sta     amyz_off+1
        lda     #$80                    ; empty bit buffer
        sta     amyz_bits

amyz_literals:
        jsr     amyz_gamma
        jsr     amyzn_lit
        jsr     amyz_getbit
        bcs     amyz_new
        jsr     amyz_gamma              ; repeat last offset

amyz_match:                             ; cpy = dst - offset
        clc
        lda     amyz_dst
        adc     amyz_off
        sta     amyz_cpy
        lda     amyz_dst+1
        adc     amyz_off+1
        sta     amyz_cpy+1
        jsr     amyzn_match
        jsr     amyz_getbit
        bcc     amyz_literals

amyz_new:
        lda     #$10                    ; sentinel: 4 bits, then carry
        sta     amyz_len
@idx:   jsr     amyz_getbit
        rol     amyz_len
        bcc     @idx                    ; len = k
        ldx     amyz_len
        cpx     #15
        beq     amyz_done
        lda     #1                      ; cpy = base = 1
        sta     amyz_cpy
        lsr     a
        sta     amyz_cpy+1
        tax                             ; j = 0
@walk:  cpx     amyz_len
        beq     @extra
        ldy     amyz_widths,x           ; w + 1
        lda     #0
        sta     amyz_off+1
        lda     #1                      ; off = 2^w
        dey
        beq     @add
@sh:    asl     a
        rol     amyz_off+1
        dey
        bne     @sh
@add:   clc
        adc     amyz_cpy
        sta     amyz_cpy
        lda     amyz_off+1
        adc     amyz_cpy+1
        sta     amyz_cpy+1
        inx
        bne     @walk                   ; always
@extra: lda     #0                      ; off = extra bits
        sta     amyz_off
        sta     amyz_off+1
        lda     amyz_widths,x
        tax
        dex
        beq     @sum
@ex:    jsr     amyz_getbit
        rol     amyz_off
        rol     amyz_off+1
        dex
        bne     @ex
@sum:   clc                             ; off = -(base + extra)
        lda     amyz_cpy
        adc     amyz_off
        eor     #$ff
        sta     amyz_off
        lda     amyz_cpy+1
        adc     amyz_off+1
        eor     #$ff
        sta     amyz_off+1
        inc     amyz_off                ; ~x + 1
        bne     :+
        inc     amyz_off+1
:       jsr     amyz_gamma
        inc     amyz_len                ; length = gamma + 1
        bne     :+
        inc     amyz_len+1
:       jmp     amyz_match

amyz_done:
        rts

; len = interlaced Elias gamma value
amyz_gamma:
        lda     #1
        sta     amyz_len
        lsr     a
        sta     amyz_len+1
@lp:    jsr     amyz_getbit
        bcs     @rts
        jsr     amyz_getbit
        rol     amyz_len
        rol     amyz_len+1
        bcc     @lp                     ; always (len < 65536)
@rts:   rts

; C = next bit. Destroys A, Y.
amyz_getbit:
        asl     amyz_bits
        bne     @rts
        ldy     #0
        lda     (amyz_src),y
        inc     amyz_src
        bne     :+
        inc     amyz_src+1
:       rol     a                       ; C was 1 (sentinel): shift it in
        sta     amyz_bits
@rts:   rts

; literal run: len stream bytes to the PPU at dst
amyzn_lit:
        lda     amyz_dst+1
        sta     PPUADDR
        lda     amyz_dst
        sta     PPUADDR
        ldy     #0
@lp:    lda     (amyz_src),y
        sta     PPUDATA
        inc     amyz_src
        bne     :+
        inc     amyz_src+1
:       inc     amyz_dst
        bne     :+
        inc     amyz_dst+1
:       jsr     amyzn_count
        bne     @lp
        rts

; match: len bytes from PPU cpy to PPU dst, one byte at a time (read back)
amyzn_match:
@lp:    lda     amyz_cpy+1
        sta     PPUADDR
        lda     amyz_cpy
        sta     PPUADDR
        lda     PPUDATA                 ; read buffer: the first read is stale
        ldx     PPUDATA
        lda     amyz_dst+1
        sta     PPUADDR
        lda     amyz_dst
        sta     PPUADDR
        stx     PPUDATA
        inc     amyz_cpy
        bne     :+
        inc     amyz_cpy+1
:       inc     amyz_dst
        bne     :+
        inc     amyz_dst+1
:       jsr     amyzn_count
        bne     @lp
        rts

; len = len - 1, Z = 1 when it reaches 0
amyzn_count:
        lda     amyz_len
        bne     :+
        dec     amyz_len+1
:       dec     amyz_len
        lda     amyz_len
        ora     amyz_len+1
        rts
