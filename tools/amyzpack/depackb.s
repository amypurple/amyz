; -----------------------------------------------------------------------------
; depackb.s - amyzpack4 BACKWARD depacker for the C64 (in-place, no data copy).
; ca65 syntax. Same as the forward depacker (depack_fwd.s) except that the
; stream is read from its end toward its start and the output is written from
; its end toward its start, with back-references at dst + offset. This is the
; AMYZ grammar read backwards ("AMYZ reverse stream", SPEC section 9):
;   stream = reverse(AMYZ(reverse(data)))
; Stage 2 copies this image to $0100 (stack page) and runs it there, so the
; image is relocated to $0100 by the packer (assembled at $1000 and $2000).
; Image (copied to $0100): [code ... fin:][jmp to the final code, appended by
; the packer]. The table (table version), the final code and the raw prefix
; bytes are copied to $0334 (tape buffer) by the same stage.
; Build options: -D TINY (no table) or the 45-byte adj/width table.
; Stream: per block, read backward: dst-end hi, dst-end lo, then the stream;
; dst hi 0 ends the list (single block: no terminator, the end branch is
; patched to fin). Entry: blk with Y = 0.
; Zero page: 9 bytes at DZP (default $57-$5f: BASIC floating-point scratch).
; -----------------------------------------------------------------------------

.ifndef DZP
DZP = $57
.endif
dst  = DZP+0            ; 2: output pointer (last byte written)
cp   = DZP+2            ; 2: match source
len  = DZP+4            ; 2: length
off  = DZP+6            ; 2: last offset (positive)
bits = DZP+8            ; bit buffer (sentinel)
kk   = cp               ; bucket index (cp is free then)
val  = cp               ; table version: 2^w | extra (cp is free then)

        .export blk, fin, tabspace, srcop, endbr

        .segment "CODE"
tabspace:
.ifndef TINY
; the 45-byte table lives outside the image, at $0334 (tape buffer), so that
; the code fits in the stack page: adj lo[15], adj hi[15], widths[15]
adjlo   = $0334
adjhi   = $0334 + 15
wid     = $0334 + 30
.endif

; store A at --dst (backward); count len down, Z = 1 when it is done (destroys X)
put:    ldx     dst
        bne     :+
        dec     dst+1
:       dec     dst
        sta     (dst),y
        lda     len
        bne     :+
        dec     len+1
:       dec     len
        lda     len
        ora     len+1
        rts

; len = interlaced Elias gamma
; (len is 0 on entry: every copy counts it down to 0, blk clears it)
gamma:  inc     len                     ; len = 1
@lp:    jsr     getbit
        bcs     @rts
        jsr     getbit
        rol     len
        rol     len+1
        bcc     @lp                     ; always
@rts:   rts

; C = next bit (destroys A)
getbit: asl     bits
        bne     @rts
        jsr     getbyte                 ; keeps C = 1 (the sentinel)
        rol     a
        sta     bits
@rts:   rts

; A = next stream byte, read backward, N/Z from it (keeps C, X, Y).
; Pre-decrement: the operand starts one past the end of the stream.
getbyte:
        lda     srcop
        bne     :+
        dec     srcop+1
:       dec     srcop
srcop = * + 1
        lda     $ffff
        rts

lit:    jsr     gamma
:       jsr     getbyte
        jsr     put
        bne     :-
        jsr     getbit
        bcs     new
        jsr     gamma                   ; repeat last offset

copy:   clc                             ; source = dst + offset (backward)
        lda     dst
        adc     off
        sta     cp
        lda     dst+1
        adc     off+1
        sta     cp+1
:       lda     cp
        bne     :+
        dec     cp+1
:       dec     cp
        lda     (cp),y
        jsr     put
        bne     :--
        jsr     getbit
        bcc     lit

new:    lda     #$10                    ; sentinel: 4 bits, then carry
        sta     kk
:       jsr     getbit
        rol     kk
        bcc     :-
.ifdef TINY
        lda     #1                      ; offset = 1, then k bits under it
        sta     off
        lsr     a
        sta     off+1
        ldx     kk                      ; k (X: Y must stay 0)
        beq     nlen
        cpx     #15
        beq     blk                     ; end of this block
endbr = * - 1                           ; single block: the packer points it at fin
:       jsr     getbit
        rol     off
        rol     off+1
        dex
        bne     :-
.else
        ldx     kk                      ; k
        cpx     #15
        beq     blk                     ; end of this block
endbr = * - 1                           ; single block: the packer points it at fin
        lda     #1
        sta     val
        lsr     a
        sta     val+1
        ldy     wid,x
        beq     :++
:       jsr     getbit
        rol     val
        rol     val+1
        dey
        bne     :-                      ; ends with Y = 0
:       clc
        lda     val
        adc     adjlo,x
        sta     off
        lda     val+1
        adc     adjhi,x
        sta     off+1
.endif
nlen:   jsr     gamma
        inc     len                     ; length = gamma + 1
        bne     copy
        inc     len+1
        bne     copy                    ; always

blk:    lda     #$80                    ; new block: empty bit buffer
        sta     bits
        jsr     getbyte                 ; dst hi (0 = end)
        beq     fin
        sta     dst+1
        jsr     getbyte
        sta     dst
        lda     #1                      ; last offset 1
        sta     off
        lsr     a
        sta     off+1
        sta     len                     ; len = 0 (see gamma)
        sta     len+1
        jmp     lit

fin:
