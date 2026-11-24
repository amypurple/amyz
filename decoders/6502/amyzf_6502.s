; -----------------------------------------------------------------------------
; amyzf_6502.s - AMYZ decompressor for the 6502, RAM to RAM, fast version
; (C64, Atari 8-bit, Apple II, BBC Micro, NES with WRAM, ...). ca65 syntax.
; Same streams as the Z80 decoders (formats/amyz/SPEC.md).
; No self-modifying code: ROM-safe (cartridges).
;
; Table: the game defines amyzf_bases, the 45-byte EXPANDED table (.tabzx):
; entry k = (1 - base[k]) lo, (1 - base[k]) hi, widths[k].
;
; Speed tricks: bit reads inlined with the bit buffer in A; gamma inlined,
; 8-bit until it overflows; copies of n <= 255 bytes run Y from -n up to 0
; on pointers pre-offset by n - 256 (lda (zp),y / sta (zp),y / iny / bne).
;
; Call:  amyz_src = stream address, amyz_dst = destination, then
;        jsr amyzf_decompress
; Out:   amyz_dst = end of output (first byte after the data).
; Uses:  A, X, Y, flags. Zero page: 11 bytes at AMYZ_ZP .. AMYZ_ZP+10.
;        Stack: 4 bytes including the caller's return address.
;        Not for in-place decoding.
; -----------------------------------------------------------------------------

.ifndef AMYZ_ZP
AMYZ_ZP = $f0                   ; relocatable: assemble with -D AMYZ_ZP=$xx
.endif

amyz_src  = AMYZ_ZP+0           ; 2: stream pointer
amyz_dst  = AMYZ_ZP+2           ; 2: output pointer
amyz_cpy  = AMYZ_ZP+4           ; 2: match source / extra bits
amyz_len  = AMYZ_ZP+6           ; 2: length / bucket index
amyz_off  = AMYZ_ZP+8           ; 2: last offset, negated
amyz_bits = AMYZ_ZP+10          ; 1: bit buffer save

        .export amyzf_decompress
        .import amyzf_bases
        .ifndef AMYZ_NOEXPORTZP         ; define when linking several decoders
        .exportzp amyz_src, amyz_dst
        .endif

        .segment "CODE"

; next bit of the buffer in A into C; the refill destroys Y
.macro  GETBIT
        asl     a
        bne     :+
        jsr     amyzf_refill
:
.endmacro

; len = interlaced Elias gamma (A = bit buffer in and out); destroys X, Y
.macro  GAMMA
        .local  lp, blp, done
        ldx     #1
        stx     amyz_len
        dex
        stx     amyz_len+1
lp:     GETBIT
        bcs     done
        GETBIT
        rol     amyz_len
        bcc     lp
        rol     amyz_len+1              ; 16-bit from here on
blp:    GETBIT
        bcs     done
        GETBIT
        rol     amyz_len
        rol     amyz_len+1
        bcc     blp                     ; always (len < 65536)
done:
.endmacro

amyzf_decompress:
        lda     #$ff                    ; last offset 1, stored as -1
        sta     amyz_off
        sta     amyz_off+1
        lda     #$80                    ; empty bit buffer

amyzf_literals:                         ; A = bit buffer
        GAMMA
        sta     amyz_bits
        ldx     amyz_len+1
        beq     lit_part
        ldy     #0                      ; whole pages first (rare)
lit_page:
        lda     (amyz_src),y
        sta     (amyz_dst),y
        iny
        bne     lit_page
        inc     amyz_src+1
        inc     amyz_dst+1
        dex
        bne     lit_page
lit_part:
        lda     amyz_len                ; n = len lo (0: done)
        beq     lit_adv
        clc                             ; src += n - 256
        adc     amyz_src
        sta     amyz_src
        lda     amyz_src+1
        adc     #$ff
        sta     amyz_src+1
        clc                             ; dst += n - 256
        lda     amyz_len
        adc     amyz_dst
        sta     amyz_dst
        lda     amyz_dst+1
        adc     #$ff
        sta     amyz_dst+1
        lda     #0
        sec
        sbc     amyz_len
        tay                             ; Y = -n
lit_lp: lda     (amyz_src),y
        sta     (amyz_dst),y
        iny
        bne     lit_lp
        inc     amyz_src+1
        inc     amyz_dst+1
lit_adv:
        lda     amyz_bits
        GETBIT
        bcc     amyzf_rep
        jmp     amyzf_new
amyzf_rep:
        GAMMA                           ; repeat last offset

amyzf_match:                            ; A = bit buffer, len = length
        sta     amyz_bits
        ldx     amyz_len+1
        beq     m_part
        clc                             ; whole pages first (rare)
        lda     amyz_dst
        adc     amyz_off
        sta     amyz_cpy
        lda     amyz_dst+1
        adc     amyz_off+1
        sta     amyz_cpy+1
        ldy     #0
m_page: lda     (amyz_cpy),y
        sta     (amyz_dst),y
        iny
        bne     m_page
        inc     amyz_cpy+1
        inc     amyz_dst+1
        dex
        bne     m_page
m_part: lda     amyz_len
        beq     m_adv
        clc                             ; dst += n - 256
        adc     amyz_dst
        sta     amyz_dst
        lda     amyz_dst+1
        adc     #$ff
        sta     amyz_dst+1
        clc                             ; cpy = dst - offset
        lda     amyz_dst
        adc     amyz_off
        sta     amyz_cpy
        lda     amyz_dst+1
        adc     amyz_off+1
        sta     amyz_cpy+1
        lda     #0
        sec
        sbc     amyz_len
        tay                             ; Y = -n
m_lp:   lda     (amyz_cpy),y
        sta     (amyz_dst),y
        iny
        bne     m_lp
        inc     amyz_dst+1
m_adv:  lda     amyz_bits
        GETBIT
        bcs     amyzf_new
        jmp     amyzf_literals

amyzf_new:                              ; A = bit buffer
        ldx     #0
        stx     amyz_len
        .repeat 4
        GETBIT
        rol     amyz_len
        .endrepeat
        ldx     amyz_len                ; k
        cpx     #15
        bne     :+
        rts                             ; end marker
:       sta     amyz_bits
        txa
        asl     a
        adc     amyz_len                ; C = 0: A = 3k
        tay
        ldx     amyzf_bases+2,y         ; width
        beq     n_nobits
        lda     amyzf_bases,y
        sta     amyz_off
        lda     amyzf_bases+1,y
        sta     amyz_off+1
        lda     #0
        sta     amyz_cpy
        sta     amyz_cpy+1
        cpx     #9
        lda     amyz_bits
        bcc     n_lo                    ; width <= 8
        pha
        txa
        sbc     #8                      ; C = 1
        tax                             ; X = width - 8
        pla
n_hi:   GETBIT
        rol     amyz_cpy+1
        dex
        bne     n_hi
        ldx     #8
n_lo:   GETBIT
        rol     amyz_cpy
        dex
        bne     n_lo
        sta     amyz_bits
        clc                             ; off = (1 - base) - extra - 1
        lda     amyz_off
        sbc     amyz_cpy
        sta     amyz_off
        lda     amyz_off+1
        sbc     amyz_cpy+1
        sta     amyz_off+1
        lda     amyz_bits
        jmp     n_len
n_nobits:
        clc                             ; off = (1 - base) - 1
        lda     amyzf_bases,y
        adc     #$ff
        sta     amyz_off
        lda     amyzf_bases+1,y
        adc     #$ff
        sta     amyz_off+1
        lda     amyz_bits
n_len:  GAMMA
        inc     amyz_len                ; length = gamma + 1
        beq     :+
        jmp     amyzf_match
:       inc     amyz_len+1
        jmp     amyzf_match

; A was shifted to 0 with C = 1 (sentinel): load the next bit byte.
; Out: A = byte << 1 | 1, C = its bit 7. Destroys Y.
amyzf_refill:
        ldy     #0
        lda     (amyz_src),y
        inc     amyz_src
        bne     :+
        inc     amyz_src+1
:       rol     a
        rts
