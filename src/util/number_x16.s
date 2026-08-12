;
; number_x16.s — MBF arithmetic via the X16 ROM math library.
;
; The library lives at $FE00..$FE99 in ROM bank 4 and is documented in
; "X16 Reference - 06 - Math Library.md". Register conventions come from
; x16-rom/math/math.inc, which is authoritative where the chapter and the
; C64 heritage disagree:
;
;   source address    .A = low, .Y = high     (movfm, conupk, fcomp, ...)
;   dest address      .X = low, .Y = high     (movmf)  <- note the swap
;
; Non-commutative operations are ARG-relative, not FAC-relative:
;   fsubt : FAC = ARG - FAC
;   fdivt : FAC = ARG / FAC
;   fpwrt : FAC = ARG ^ FAC
; so for `acc OP b` the accumulator goes into ARG and the operand into FAC.
; math.inc's comments say the opposite ("FAC -= ARG"); the C64 originals and
; chapter 06 agree with what is written here, and test_number checks it.
;
; The X16's faddt/fmultt/fdivt/fpwrt are the "FIXED VERSION" entry points
; that do their own sign and flag setup, unlike the C64 originals which are
; unusable without poking library internals first.
;
; ZERO PAGE: the library works in ZPMATH, $A9..$D3 (from x16-rom's
; build/x16/basic.map). cc65's zero page is $22..$7F. They do not overlap,
; which is what makes calling this from C safe at all. _snprim_zp_probe
; verifies it on real hardware rather than trusting the map.
;

        .export _snprim_from_i16, _snprim_cmp
        .export _snprim_add, _snprim_sub, _snprim_mul, _snprim_div
        .export _snprim_pow, _snprim_sqrt
        .export _snprim_neg, _snprim_abs, _snprim_int
        .export _snprim_to_i32, _snprim_to_text, _snprim_zp_probe

        .import popax
        .importzp ptr1, ptr2, sreg

; --- ROM math library entry points ----------------------------------
givayf  = $FE03                 ; FAC = (s16) .A:.Y   (.A high, .Y low)
fout    = $FE06                 ; FAC -> ASCIIZ at fbuffr
fsubt   = $FE15                 ; FAC = ARG - FAC
faddt   = $FE1B                 ; FAC = ARG + FAC
fmultt  = $FE21                 ; FAC = ARG * FAC
fdivt   = $FE27                 ; FAC = ARG / FAC
int     = $FE2D                 ; FAC = floor(FAC)
sqr     = $FE30                 ; FAC = sqrt(FAC)
negop   = $FE33                 ; FAC = -FAC
fpwrt   = $FE39                 ; FAC = ARG ^ FAC
abs     = $FE4E                 ; FAC = |FAC|
fcomp   = $FE54                 ; .A = sign(FAC - mem(.Y:.A))
conupk  = $FE5A                 ; ARG = mem(.Y:.A)
movfm   = $FE63                 ; FAC = mem(.Y:.A)
movmf   = $FE66                 ; mem(.Y:.X) = round(FAC)
qint    = $FE8D                 ; facho..facho+3 = (s32) FAC, big-endian

fbuffr  = $0100                 ; fout writes its result here
facho   = $00C4                 ; FAC mantissa, where qint leaves its result

SNUM_TEXT_MAX = 24              ; must match number.h

ROM_BANK = $01
MATH_BANK = 4

; Golden RAM ($0400-$07FF) rather than bss -- see cfg/x16sheet.cfg. Both of
; these are saved-and-restored scratch, written before they are read on every
; call, so the area not being cleared by the startup code does not matter to
; them. 95 bytes is a sixth of the resident bss this program had.
        .segment "GOLDENBSS"
saved_bank:  .res 1
zp_shadow:   .res 94            ; $22..$7F

        .code

; --- prologues -------------------------------------------------------
; Two-pointer entry: .A/.X hold the second argument, the first is on the
; C stack. Leaves ptr1 = first, ptr2 = second, ROM bank switched to 4.
.proc   prolog2
        sta     ptr2
        stx     ptr2+1
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        ; fall through
.endproc

.proc   bank_in
        lda     ROM_BANK
        sta     saved_bank
        lda     #MATH_BANK
        sta     ROM_BANK
        rts
.endproc

; One-pointer entry: .A/.X hold the only argument.
.proc   prolog1
        sta     ptr1
        stx     ptr1+1
        jmp     bank_in
.endproc

.proc   bank_out
        lda     saved_bank
        sta     ROM_BANK
        rts
.endproc

; Store FAC back into *ptr1, restore the ROM bank, return.
.proc   store_ptr1
        ldx     ptr1            ; movmf takes the destination in .Y:.X
        ldy     ptr1+1
        jsr     movmf
        jmp     bank_out
.endproc

; ARG = *ptr2, FAC = *ptr1  (for the commutative operations)
.proc   load_arg2_fac1
        lda     ptr2
        ldy     ptr2+1
        jsr     conupk
        lda     ptr1
        ldy     ptr1+1
        jmp     movfm
.endproc

; ARG = *ptr1, FAC = *ptr2  (for ARG-relative subtract, divide, power)
.proc   load_arg1_fac2
        lda     ptr1
        ldy     ptr1+1
        jsr     conupk
        lda     ptr2
        ldy     ptr2+1
        jmp     movfm
.endproc

; --- void __fastcall__ snprim_add(snum_t *acc, const snum_t *b) ------
.proc   _snprim_add
        jsr     prolog2
        jsr     load_arg2_fac1
        jsr     faddt
        jmp     store_ptr1
.endproc

.proc   _snprim_sub
        jsr     prolog2
        jsr     load_arg1_fac2  ; ARG = acc, FAC = b
        jsr     fsubt           ; FAC = ARG - FAC = acc - b
        jmp     store_ptr1
.endproc

.proc   _snprim_mul
        jsr     prolog2
        jsr     load_arg2_fac1
        jsr     fmultt
        jmp     store_ptr1
.endproc

.proc   _snprim_div
        jsr     prolog2
        jsr     load_arg1_fac2  ; ARG = acc, FAC = b
        jsr     fdivt           ; FAC = ARG / FAC = acc / b
        jmp     store_ptr1
.endproc

.proc   _snprim_pow
        jsr     prolog2
        jsr     load_arg1_fac2  ; ARG = acc, FAC = e
        jsr     fpwrt           ; FAC = ARG ^ FAC = acc ^ e
        jmp     store_ptr1
.endproc

; --- single-operand, in place ---------------------------------------
.macro  UNARY name, rom_call
.proc   name
        jsr     prolog1
        lda     ptr1
        ldy     ptr1+1
        jsr     movfm
        jsr     rom_call
        jmp     store_ptr1
.endproc
.endmacro

        UNARY _snprim_sqrt, sqr
        UNARY _snprim_neg,  negop
        UNARY _snprim_abs,  abs
        UNARY _snprim_int,  int

; --- uint8_t __fastcall__ snprim_cmp(const snum_t *a, const snum_t *b)
; Returns the ROM's own encoding: 0 equal, 1 a > b, $FF a < b.
.proc   _snprim_cmp
        jsr     prolog2
        lda     ptr1
        ldy     ptr1+1
        jsr     movfm           ; FAC = *a
        lda     ptr2
        ldy     ptr2+1
        jsr     fcomp           ; .A = comparison result
        pha
        jsr     bank_out
        pla
        ldx     #$00
        rts
.endproc

; --- void __fastcall__ snprim_from_i16(snum_t *r, int16_t v) ---------
; cc65 passes the 16-bit v in .A (low) / .X (high); givayf wants .A high,
; .Y low, so the two are exchanged.
.proc   _snprim_from_i16
        sta     ptr2            ; v low
        stx     ptr2+1          ; v high
        jsr     popax
        sta     ptr1
        stx     ptr1+1
        jsr     bank_in
        lda     ptr2+1          ; .A = high
        ldy     ptr2            ; .Y = low
        jsr     givayf
        jmp     store_ptr1
.endproc

; --- int32_t __fastcall__ snprim_to_i32(const snum_t *a) -------------
; qint leaves a big-endian signed 32-bit result in facho..facho+3.
; cc65 returns a long in sreg:.X:.A, little-endian by significance.
.proc   _snprim_to_i32
        jsr     prolog1
        lda     ptr1
        ldy     ptr1+1
        jsr     movfm
        jsr     qint
        lda     facho+3         ; least significant
        sta     ptr2
        lda     facho+2
        sta     ptr2+1
        lda     facho+1
        sta     sreg
        lda     facho+0         ; most significant
        sta     sreg+1
        jsr     bank_out
        lda     ptr2
        ldx     ptr2+1
        rts
.endproc

; --- uint8_t __fastcall__ snprim_to_text(const snum_t *a, char *out) -
; fout follows the CBM convention of reserving the sign column, so a
; non-negative result arrives with a leading space: " 42", not "42". A
; spreadsheet right-aligns its own cells and measures the width it is given,
; so that space is stripped here rather than left for every caller to
; remember. The host backend produces no space, and this is what makes the
; two agree.
.proc   _snprim_to_text
        jsr     prolog2         ; ptr1 = a, ptr2 = out
        lda     ptr1
        ldy     ptr1+1
        jsr     movfm
        jsr     fout            ; ASCIIZ result at fbuffr ($0100)
        ldx     #$00            ; .X reads fbuffr, .Y writes through ptr2
        ldy     #$00
        lda     fbuffr
        cmp     #$20
        bne     @copy
        inx
@copy:  lda     fbuffr,x
        sta     (ptr2),y
        beq     @done
        inx
        iny
        cpy     #SNUM_TEXT_MAX-1
        bne     @copy
        lda     #$00
        sta     (ptr2),y
@done:  tya                     ; .Y = length, excluding the terminator
        pha
        jsr     bank_out
        pla
        ldx     #$00
        rts
.endproc

; --- uint8_t snprim_zp_probe(void) ----------------------------------
; Snapshot cc65's zero page, run a full arithmetic sequence through the ROM
; math library, and count the bytes that moved. Uses only absolute
; addressing and the registers, so anything it reports was caused by the
; library and not by this routine.
.proc   _snprim_zp_probe
        ldy     #$00
@save:  lda     $22,y
        sta     zp_shadow,y
        iny
        cpy     #94
        bne     @save

        lda     ROM_BANK
        sta     saved_bank
        lda     #MATH_BANK
        sta     ROM_BANK

        lda     #<c_981                 ; FAC = 9.81
        ldy     #>c_981
        jsr     movfm
        lda     #<c_5                   ; ARG = 5
        ldy     #>c_5
        jsr     conupk
        jsr     fmultt                  ; FAC = 49.05
        lda     #<c_2
        ldy     #>c_2
        jsr     conupk
        jsr     fdivt                   ; FAC = ARG / FAC -> 2 / 49.05
        jsr     fout                    ; exercise the formatter too
        lda     #<c_981
        ldy     #>c_981
        jsr     movfm
        jsr     qint

        lda     saved_bank
        sta     ROM_BANK

        ldx     #$00                    ; count differences
        ldy     #$00
@cmp:   lda     $22,y
        cmp     zp_shadow,y
        beq     @next
        inx
@next:  iny
        cpy     #94
        bne     @cmp
        txa
        ldx     #$00
        rts
.endproc

        .rodata
c_981:  .byte $84, $1C, $F5, $C2, $8F   ; 9.81
c_5:    .byte $83, $20, $00, $00, $00   ; 5.0
c_2:    .byte $82, $00, $00, $00, $00   ; 2.0
