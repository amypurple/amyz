; -----------------------------------------------------------------------------
; amyzs_6502.s - AMYZ decompressor for the 6502, RAM to RAM, SELF-MODIFYING
; fast version for machines that run code from RAM (C64, Atari 8-bit,
; Apple II, BBC Micro). NOT ROM-SAFE: it patches the operands of its copy
; loops and of the bit refill. For cartridges / ROM use amyzs_6502.s.
; ca65 syntax. Same streams as all AMYZ decoders (formats/amyz/SPEC.md).
;
; Table: the game defines amyzf_bases, the 45-byte EXPANDED table (.tabzx).
; Same structure as amyzs_6502.s; differences: the stream pointer lives in
; the operand of the refill's "lda abs" (no Y needed), and the copy loops
; are "lda abs,y / sta abs,y / iny / bne" (14 cycles per byte, no page
; crossing on the store) on patched operands.
;
; Call:  amyz_src = stream address, amyz_dst = destination, then
;        jsr amyzs_decompress
; Out:   amyz_dst = end of output (first byte after the data).
; Uses:  A, X, Y, flags. Zero page: 11 bytes at AMYZ_ZP .. AMYZ_ZP+10
;        (amyz_src is only read at entry).
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

        .export amyzs_decompress
        .import amyzf_bases
        .ifndef AMYZ_NOEXPORTZP         ; define when linking several decoders
        .exportzp amyz_src, amyz_dst
        .endif

        .segment "CODE"

; next bit of the buffer in A into C
.macro  GETBIT
        asl     a
        bne     :+
        jsr     amyzs_refill
:
.endmacro

; len = interlaced Elias gamma (A = bit buffer in and out); destroys X
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

amyzs_decompress:
        lda     amyz_src                ; stream pointer -> refill operand
        sta     amyzs_sp
        lda     amyz_src+1
        sta     amyzs_sp+1
        lda     #$ff                    ; last offset 1, stored as -1
        sta     amyz_off
        sta     amyz_off+1
        lda     #$80                    ; empty bit buffer

amyzs_literals:                         ; A = bit buffer
        GAMMA
        sta     amyz_bits
        ldx     amyz_len+1
        beq     lit_part
        lda     amyzs_sp                ; whole pages first (rare)
        sta     lit_ps
        lda     amyzs_sp+1
        sta     lit_ps+1
        lda     amyz_dst
        sta     lit_pd
        lda     amyz_dst+1
        sta     lit_pd+1
        ldy     #0
lit_ps = * + 1
lit_page:
        lda     $ffff,y
lit_pd = * + 1
        sta     $ffff,y
        iny
        bne     lit_page
        inc     lit_ps+1
        inc     lit_pd+1
        inc     amyzs_sp+1
        inc     amyz_dst+1
        dex
        bne     lit_page
lit_part:
        lda     amyz_len                ; n = len lo (0: done)
        beq     lit_adv
        clc                             ; src' = src + n - 256
        adc     amyzs_sp
        sta     amyzs_sp
        sta     lit_s
        lda     amyzs_sp+1
        adc     #$ff
        sta     lit_s+1
        tax
        inx
        stx     amyzs_sp+1              ; src += n
        clc                             ; dst' = dst + n - 256
        lda     amyz_len
        adc     amyz_dst
        sta     amyz_dst
        sta     lit_d
        lda     amyz_dst+1
        adc     #$ff
        sta     lit_d+1
        tax
        inx
        stx     amyz_dst+1              ; dst += n
        lda     #0
        sec
        sbc     amyz_len
        tay                             ; Y = -n
lit_s = * + 1
lit_lp: lda     $ffff,y
lit_d = * + 1
        sta     $ffff,y
        iny
        bne     lit_lp
lit_adv:
        lda     amyz_bits
        GETBIT
        bcc     amyzs_rep
        jmp     amyzs_new
amyzs_rep:
        GAMMA                           ; repeat last offset

amyzs_match:                            ; A = bit buffer, len = length
        sta     amyz_bits
        ldx     amyz_len+1
        beq     m_part
        clc                             ; whole pages first (rare)
        lda     amyz_dst
        sta     m_pd
        adc     amyz_off
        sta     m_ps
        lda     amyz_dst+1
        sta     m_pd+1
        adc     amyz_off+1
        sta     m_ps+1
        ldy     #0
m_ps = * + 1
m_page: lda     $ffff,y
m_pd = * + 1
        sta     $ffff,y
        iny
        bne     m_page
        inc     m_ps+1
        inc     m_pd+1
        inc     amyz_dst+1
        dex
        bne     m_page
m_part: lda     amyz_len
        beq     m_adv
        clc                             ; dst' = dst + n - 256
        adc     amyz_dst
        sta     amyz_dst
        sta     m_d
        tax
        lda     amyz_dst+1
        adc     #$ff
        sta     m_d+1
        tay
        clc                             ; src' = dst' - offset
        txa
        adc     amyz_off
        sta     m_s
        tya
        adc     amyz_off+1
        sta     m_s+1
        iny
        sty     amyz_dst+1              ; dst += n
        lda     #0
        sec
        sbc     amyz_len
        tay                             ; Y = -n
m_s = * + 1
m_lp:   lda     $ffff,y
m_d = * + 1
        sta     $ffff,y
        iny
        bne     m_lp
m_adv:  lda     amyz_bits
        GETBIT
        bcs     amyzs_new
        jmp     amyzs_literals

amyzs_new:                              ; A = bit buffer
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
        jmp     amyzs_match
:       inc     amyz_len+1
        jmp     amyzs_match

; A was shifted to 0 with C = 1 (sentinel): load the next bit byte.
; Out: A = byte << 1 | 1, C = its bit 7.
amyzs_refill:
amyzs_sp = * + 1
        lda     $ffff                   ; stream pointer (patched)
        inc     amyzs_sp
        bne     :+
        inc     amyzs_sp+1
:       rol     a
        rts
