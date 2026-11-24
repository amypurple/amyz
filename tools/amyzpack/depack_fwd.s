; -----------------------------------------------------------------------------
; depack.s - amyzpack 2 depacker for the C64: AMYZ decoder optimised for size.
; ca65 syntax. Output and match pointers live in zero page with Y = 0
; (indirect indexed); the stream pointer is a patched operand.
;
; Image layout (the packer relocates it: it is assembled at $1000 and $2000,
; the high bytes that differ are patched):
;   [table space][code ... fin:][final bytes appended by the packer]
; Build options:
;   -D TINY    fixed log2 buckets (AMYZ tiny mode): no table at all
;   (default)  45-byte table: adj lo[15], adj hi[15], widths[15], with
;              adj[k] = base[k] - 2^widths[k]: the extra bits are read under
;              a leading 1, so offset = adj[k] + (2^w | extra)
;   -D TAIL    the final bytes are copied to $0100 and run there (needed when
;              this code sits under a ROM: $a000-$bfff or $d000-$ffff);
;              without TAIL they run in place right after the code.
; The packed stream: per block, dst hi, dst lo, then an AMYZ stream; a dst hi
; of 0 ends the list (with a single block the packer patches the end-of-block
; branch to go straight to fin, and drops that 0). Entry: blk, with Y = 0 (stage 1 leaves it so).
; Zero page: 9 bytes at DZP (default $57-$5f: BASIC floating-point scratch).
; Self-modifying: the stream pointer is the operand of getbyte's lda.
; -----------------------------------------------------------------------------

.ifndef DZP
DZP = $57
.endif
dst  = DZP+0            ; 2: output pointer
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
adjlo:  .res    15
adjhi:  .res    15
wid:    .res    15
.endif

; store A at the output and advance; count len down, Z = 1 when it is done
put:    sta     (dst),y
        inc     dst
        bne     :+
        inc     dst+1
:       lda     len
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

; A = next stream byte, N/Z from it (keeps C, X, Y). Pre-increment: the
; operand points at the previous byte (the packer stores stream start - 1).
getbyte:
        inc     srcop
        bne     :+
        inc     srcop+1
srcop = * + 1
:       lda     $ffff
        rts

lit:    jsr     gamma
:       jsr     getbyte
        jsr     put
        bne     :-
        jsr     getbit
        bcs     new
        jsr     gamma                   ; repeat last offset

copy:   sec                             ; source = dst - offset
        lda     dst
        sbc     off
        sta     cp
        lda     dst+1
        sbc     off+1
        sta     cp+1
:       lda     (cp),y
        inc     cp
        bne     :+
        inc     cp+1
:       jsr     put
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
.ifdef TAIL
        ldx     #$ff                    ; patched: final bytes - 1
tlen = * - 1
:       lda     tailb,x
        sta     $0100,x
        dex
        bpl     :-
        jmp     $0100
tailb:
        .export tlen
.endif
