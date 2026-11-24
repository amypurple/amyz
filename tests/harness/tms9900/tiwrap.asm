* test wrapper: decoder at >A000, table at >B000, workspace at >8300
AMYZWS EQU  >8300
AMYZW  EQU  >B000
       AORG >A000
       COPY "../tms9900/amyz_tms9900.asm"
       END
