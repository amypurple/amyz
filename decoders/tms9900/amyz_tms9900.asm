* -----------------------------------------------------------------------------
* amyz_tms9900.asm - AMYZ decompressor for the TMS9900 (TI-99/4A), to VRAM.
* xas99 syntax. Same streams as the Z80 decoders (formats/amyz/SPEC.md).
* The TI-99/4A has the TMS9918A VDP of the ColecoVision:
*   >8C00 write data, >8C02 write address, >8800 read data.
* Back-references are read back from VRAM (like z80/amyz_vram.asm).
*
* Table: the game defines AMYZW, its 15-byte table (.tabz, width + 1 per byte).
* Call:  BLWP @AMYZV   with caller R0 = stream address (CPU memory),
*                           caller R1 = VRAM destination address.
* Out:   caller R1 = VRAM address after the last byte written.
* RAM:   the 32-byte workspace AMYZWS (default >8300 scratchpad; set AMYZWS
*        with EQU before including this file to move it). No other RAM.
*        Interrupts: the caller should run with LIMI 0 (VDP address set).
* -----------------------------------------------------------------------------

VDPWD  EQU  >8C00
VDPWA  EQU  >8C02
VDPRD  EQU  >8800

* register use: R1 stream, R2 VRAM destination, R3 bit buffer (sentinel),
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

AMYZV  DATA AMYZWS,AMYZ0

AMYZ0  MOV  *R13,R1              stream
       MOV  @2(R13),R2           VRAM destination
       SETO R6                   last offset 1, stored as -1
       LI   R3,>8000             empty bit buffer

* literal run
AMYZL  BL   @AMYZG               R4 = run length
       MOV  R2,R7
       ORI  R7,>4000
       SWPB R7
       MOVB R7,@VDPWA
       SWPB R7
       MOVB R7,@VDPWA            VRAM write address
       A    R4,R2
AMYZL1 MOVB *R1+,@VDPWD
       DEC  R4
       JNE  AMYZL1
       .GETBIT
       JOC  AMYZN
       BL   @AMYZG               repeat last offset: R4 = length

* copy R4 bytes from VRAM (R2 - offset) to VRAM R2, one byte at a time
AMYZC  MOV  R2,R5
       A    R6,R5                R5 = source
AMYZC1 SWPB R5
       MOVB R5,@VDPWA
       SWPB R5
       MOVB R5,@VDPWA            VRAM read address
       INC  R5
       MOV  R2,R7
       ORI  R7,>4000
       MOVB @VDPRD,R0
       SWPB R7
       MOVB R7,@VDPWA
       SWPB R7
       MOVB R7,@VDPWA            VRAM write address
       INC  R2
       MOVB R0,@VDPWD
       DEC  R4
       JNE  AMYZC1
       .GETBIT
       JNC  AMYZL

* new offset: 4-bit bucket index k, then widths[k] extra bits
AMYZN  CLR  R5
       LI   R9,4
AMYZN1 SLA  R5,1
       .GETBIT
       JNC  AMYZN2
       INC  R5
AMYZN2 DEC  R9
       JNE  AMYZN1
       CI   R5,15
       JEQ  AMYZE                end marker
       LI   R8,1                 base = 1
       CLR  R9                   j = 0
AMYZB1 C    R9,R5
       JEQ  AMYZB3
       MOVB @AMYZW(R9),R0
       SRL  R0,8
       DEC  R0                   R0 = widths[j]
       LI   R10,1
       MOV  R0,R0
       JEQ  AMYZB2               (a shift count of 0 in R0 would mean 16)
       SLA  R10,0                R10 = 2^widths[j]
AMYZB2 A    R10,R8
       INC  R9
       JMP  AMYZB1
AMYZB3 MOVB @AMYZW(R5),R9
       SRL  R9,8
       DEC  R9                   R9 = widths[k]
       CLR  R10                  extra bits
       MOV  R9,R9
       JEQ  AMYZX2
AMYZX1 SLA  R10,1
       .GETBIT
       JNC  AMYZX3
       INC  R10
AMYZX3 DEC  R9
       JNE  AMYZX1
AMYZX2 A    R10,R8               offset = base + extra
       MOV  R8,R6
       NEG  R6                   R6 = -offset
       BL   @AMYZG
       INC  R4                   length = gamma + 1
       JMP  AMYZC

AMYZE  MOV  R2,@2(R13)           caller R1 = end address
       RTWP

* R4 = interlaced Elias gamma value
AMYZG  LI   R4,1
AMYZG1 .GETBIT
       JOC  AMYZG3
       SLA  R4,1
       .GETBIT
       JNC  AMYZG1
       INC  R4
       JMP  AMYZG1
AMYZG3 B    *R11
