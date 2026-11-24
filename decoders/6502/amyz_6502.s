; -----------------------------------------------------------------------------
; amyz_6502.s - AMYZ decompressor for the 6502, RAM to RAM, small version
; (C64, Atari 8-bit, Apple II, BBC Micro, NES with WRAM, ...). ca65 syntax.
; Same streams as the Z80 decoders (formats/amyz/SPEC.md).
;
; Table: the game defines amyz_widths, its 15-byte table (.tabz), each byte =
; bucket width + 1. The decoder computes base[k] at every new offset.
;
; Call:  amyz_src = stream address, amyz_dst = destination, then
;        jsr amyz_decompress
; Out:   amyz_dst = end of output (first byte after the data).
; Uses:  A, X, Y, flags. Zero page: 11 bytes at AMYZ_ZP .. AMYZ_ZP+10.
;        Stack: 6 bytes including the caller's return address.
;        No self-modifying code (ROM-safe). Not for in-place decoding.
; -----------------------------------------------------------------------------

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
        lda     #$ff                    ; last offset 1, stored as -1
        sta     amyz_off
        sta     amyz_off+1
        lda     #$80                    ; empty bit buffer
        sta     amyz_bits

amyz_literals:
        jsr     amyz_gamma
        lda     amyz_src
        sta     amyz_cpy
        lda     amyz_src+1
        sta     amyz_cpy+1
        jsr     amyz_copy
        lda     amyz_cpy
        sta     amyz_src
        lda     amyz_cpy+1
        sta     amyz_src+1
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
        jsr     amyz_copy
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

; copy len bytes from (cpy) to (dst), forward; advances cpy and dst
amyz_copy:
        ldy     #0
        ldx     amyz_len+1
        beq     @part
@page:  lda     (amyz_cpy),y
        sta     (amyz_dst),y
        iny
        bne     @page
        inc     amyz_cpy+1
        inc     amyz_dst+1
        dex
        bne     @page
@part:  ldx     amyz_len
        beq     @rts
@lp:    lda     (amyz_cpy),y
        sta     (amyz_dst),y
        iny
        dex
        bne     @lp
        tya
        clc
        adc     amyz_dst
        sta     amyz_dst
        bcc     :+
        inc     amyz_dst+1
        clc
:       tya
        adc     amyz_cpy
        sta     amyz_cpy
        bcc     @rts
        inc     amyz_cpy+1
@rts:   rts
