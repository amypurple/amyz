* -----------------------------------------------------------------------------
* amyz_tms9900_ram.asm - AMYR decompressor for the TMS9900, RAM to RAM.
* xas99 syntax. Same streams as the Z80 decoders (formats/amyz/SPEC.md).
* Not for in-place decoding. Labels are prefixed AMYR.
*
* Table: the game defines AMYZW, its 15-byte table (.tabz, width + 1 per byte).
* Call:  BLWP @AMYRV   with caller R0 = stream address (CPU memory),
*                           caller R1 = destination address (CPU RAM).
* Out:   caller R1 = address after the last byte written.
* RAM:   the 32-byte workspace AMYZWS (default >8300 scratchpad; set AMYZWS
*        with EQU before including this file to move it). No other RAM.
* -----------------------------------------------------------------------------

* register use: R1 stream, R2 destination, R3 bit buffer (sentinel),
* R4 length, R5 index / source, R6 -last offset, R7 scratch, R8 base,
* R9 bucket counter, R10 scratch, R11 link, R13-R15 BLWP.

* next bit into C: shift the buffer; when only the sentinel was left, load
* the next stream byte into the high byte, set a new sentinel below it and
* shift again.
       .defm GETBIT
       SLA  R3,1
       JNE  $+10
       MOVB *R1+,R3
       ORI  R3,>0080
       SLA  R3,1
       .endm

AMYRV  DATA AMYZWS,AMYR0

AMYR0  MOV  *R13,R1              stream
       MOV  @2(R13),R2           destination
       SETO R6                   last offset 1, stored as -1
       LI   R3,>8000             empty bit buffer

* literal run
AMYRL  BL   @AMYRG               R4 = run length
AMYRL1 MOVB *R1+,*R2+
       DEC  R4
       JNE  AMYRL1
       .GETBIT
       JOC  AMYRN
       BL   @AMYRG               repeat last offset: R4 = length

* copy R4 bytes from R2 - offset to R2, forward, byte by byte
AMYRC  MOV  R2,R5
       A    R6,R5                R5 = source
AMYRC1 MOVB *R5+,*R2+
       DEC  R4
       JNE  AMYRC1
       .GETBIT
       JNC  AMYRL

* new offset: 4-bit bucket index k, then widths[k] extra bits
AMYRN  CLR  R5
       LI   R9,4
AMYRN1 SLA  R5,1
       .GETBIT
       JNC  AMYRN2
       INC  R5
AMYRN2 DEC  R9
       JNE  AMYRN1
       CI   R5,15
       JEQ  AMYRE                end marker
       LI   R8,1                 base = 1
       CLR  R9                   j = 0
AMYRB1 C    R9,R5
       JEQ  AMYRB3
       MOVB @AMYZW(R9),R0
       SRL  R0,8
       DEC  R0                   R0 = widths[j]
       LI   R10,1
       MOV  R0,R0
       JEQ  AMYRB2               (a shift count of 0 in R0 would mean 16)
       SLA  R10,0                R10 = 2^widths[j]
AMYRB2 A    R10,R8
       INC  R9
       JMP  AMYRB1
AMYRB3 MOVB @AMYZW(R5),R9
       SRL  R9,8
       DEC  R9                   R9 = widths[k]
       CLR  R10                  extra bits
       MOV  R9,R9
       JEQ  AMYRX2
AMYRX1 SLA  R10,1
       .GETBIT
       JNC  AMYRX3
       INC  R10
AMYRX3 DEC  R9
       JNE  AMYRX1
AMYRX2 A    R10,R8               offset = base + extra
       MOV  R8,R6
       NEG  R6                   R6 = -offset
       BL   @AMYRG
       INC  R4                   length = gamma + 1
       JMP  AMYRC

AMYRE  MOV  R2,@2(R13)           caller R1 = end address
       RTWP

* R4 = interlaced Elias gamma value
AMYRG  LI   R4,1
AMYRG1 .GETBIT
       JOC  AMYRG3
       SLA  R4,1
       .GETBIT
       JNC  AMYRG1
       INC  R4
       JMP  AMYRG1
AMYRG3 B    *R11
