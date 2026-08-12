;
; bankmem_acc_x16.s — the banked-RAM accessors, in assembly.
;
; These are the narrowest and most-used routines in the program: every cell
; the renderer draws, every node of the sparse index, every byte of a blob
; goes through one of them. In C each was a bank-register store followed by
; a pointer computation cc65 builds with its 16-bit helpers; here each is a
; handful of instructions.
;
; The window is 8 KB at $A000, so an offset is always below $2000 and adding
; $A0 to its high byte can never carry. That is what makes these as short as
; they are, and it is a property of the hardware rather than an assumption:
; a block that straddled a bank boundary would break it, which is exactly
; why the allocator never places one.
;
; The bulk operations (read, write, set) stay in C: they are memcpy and
; memset with a bank store in front, and cc65's library versions are already
; better than anything worth hand-writing here.
;
; EVERY ROUTINE HERE MUST MATCH ITS PROTOTYPE IN bankmem.h ARGUMENT FOR
; ARGUMENT. cc65 passes the last argument in registers and the rest on the C
; stack, so a routine written for one argument count and called with another
; does not fail to link or to run — it silently reads the wrong values and
; corrupts whatever the resulting pointer happens to address. bankmem_map
; was written for two arguments when the header said three, and the symptom
; was a crash in an unrelated part of the program a hundred cells later.
;

        .export         _bankmem_peek, _bankmem_poke
        .export         _bankmem_peek16, _bankmem_poke16
        .export         _bankmem_peek32, _bankmem_poke32
        .export         _bankmem_map

        .import         popa, popax
        .importzp       ptr1, tmp1, tmp2, sreg

RAM_BANK = $00
WINDOW_HI = $A0

; Turn the offset in .A/.X into a pointer in ptr1, and select `bank`.
; Leaves .Y = 0, ready for indirect indexed access.
.macro  SETUP_PTR
        sta     ptr1
        stx     ptr1+1
        jsr     popa                    ; bank
        sta     RAM_BANK
        lda     ptr1+1
        clc
        adc     #WINDOW_HI              ; offset < $2000, so never carries
        sta     ptr1+1
        ldy     #$00
.endmacro

; --- uint8_t __fastcall__ bankmem_peek(uint8_t bank, uint16_t off) ----
.proc   _bankmem_peek
        SETUP_PTR
        lda     (ptr1),y
        ldx     #$00
        rts
.endproc

; --- void __fastcall__ bankmem_poke(uint8_t bank, uint16_t off, uint8_t v)
.proc   _bankmem_poke
        sta     tmp1                    ; value (last argument)
        jsr     popax                   ; offset
        SETUP_PTR
        lda     tmp1
        sta     (ptr1),y
        rts
.endproc

; --- uint16_t __fastcall__ bankmem_peek16(uint8_t bank, uint16_t off) -
; Little-endian in banked RAM, and cc65 returns 16 bits in .A low, .X high.
.proc   _bankmem_peek16
        SETUP_PTR
        lda     (ptr1),y
        pha
        iny
        lda     (ptr1),y
        tax
        pla
        rts
.endproc

; --- void __fastcall__ bankmem_poke16(uint8_t bank, uint16_t off, uint16_t v)
.proc   _bankmem_poke16
        sta     tmp1                    ; value low
        stx     tmp2                    ; value high
        jsr     popax                   ; offset
        SETUP_PTR
        lda     tmp1
        sta     (ptr1),y
        iny
        lda     tmp2
        sta     (ptr1),y
        rts
.endproc

; --- handle_t __fastcall__ bankmem_peek32(uint8_t bank, uint16_t off) -
; cc65 returns a long in sreg (high word) and .X:.A (low word).
.proc   _bankmem_peek32
        SETUP_PTR
        lda     (ptr1),y
        sta     tmp1                    ; byte 0
        iny
        lda     (ptr1),y
        sta     tmp2                    ; byte 1
        iny
        lda     (ptr1),y
        sta     sreg                    ; byte 2
        iny
        lda     (ptr1),y
        sta     sreg+1                  ; byte 3
        lda     tmp1
        ldx     tmp2
        rts
.endproc

; --- void __fastcall__ bankmem_poke32(uint8_t bank, uint16_t off, handle_t v)
; The value arrives in sreg:.X:.A; the offset and bank are on the C stack.
.proc   _bankmem_poke32
        sta     tmp1                    ; byte 0
        stx     tmp2                    ; byte 1
        jsr     popax                   ; offset
        SETUP_PTR
        lda     tmp1
        sta     (ptr1),y
        iny
        lda     tmp2
        sta     (ptr1),y
        iny
        lda     sreg
        sta     (ptr1),y
        iny
        lda     sreg+1
        sta     (ptr1),y
        rts
.endproc

; --- void * __fastcall__ bankmem_map(uint8_t bank, uint16_t off,
;                                     uint16_t len) --------------------
;
; THREE arguments, not two. `len` is what the host backend range-checks;
; this backend ignores it, because the allocator guarantees a block never
; crosses a bank. But it still arrives in .A/.X as the last argument, so it
; has to be stepped over before the offset can be popped.
;
; Getting this wrong is silent and vicious: the routine then takes `len` as
; the offset and the offset's low byte as the bank, hands back a pointer
; into an arbitrary bank, and the caller's memmove writes through it. The
; symptom is corruption somewhere else entirely — in this case stray writes
; across the I/O page.
.proc   _bankmem_map
        jsr     popax                   ; discard .A/.X (len), fetch offset
        SETUP_PTR
        lda     ptr1
        ldx     ptr1+1
        rts
.endproc
