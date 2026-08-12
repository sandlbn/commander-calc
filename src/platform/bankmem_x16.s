;
; bankmem_x16.s — how much banked RAM this machine actually has.
;
; MEMTOP ($FF99) with carry set returns the number of usable 8 KB RAM banks
; in .A: $40 for 512 KB, $80 for 1 MB, $00 for 2 MB (i.e. 256). It lives in
; the KERNAL jump table at the top of the address space, so it is reachable
; from any ROM bank and needs no banking of its own.
;
; uint8_t bankmem_memtop_banks(void);
;

        .export         _bankmem_memtop_banks

MEMTOP  = $FF99

.proc   _bankmem_memtop_banks
        sec                     ; carry set = get
        jsr     MEMTOP          ; .A = bank count, .X/.Y = top of RAM
        ldx     #$00            ; cc65 returns 8-bit values in .A, .X = 0
        rts
.endproc
