;
; screen_x16.s — charset selection.
;
; screen_set_charset ($FF62) with .A = 1 uploads the ISO-8859-15 character
; set and activates it. In ISO mode the VERA tile index is the character
; code itself, so ASCII written to the tile map displays as ASCII. Every
; other charset needs PETSCII screen-code translation on every character,
; which for a spreadsheet full of imported text is a cost with no upside.
;
; void screen_set_iso_charset(void);
;

        .export         _screen_set_iso_charset
        .export         _screen_get_size

screen_set_charset = $FF62
screen_mode        = $FF5F
bsout              = $FFD2

; Two halves of the same decision, and the second one is easy to miss.
;
; screen_set_charset puts the ISO GLYPHS in VRAM, which is what makes a tile
; index equal a character code. It does not touch the KEYBOARD, and the
; keyboard driver stays in PETSCII: an unshifted letter arrives as $41..$5A
; and a shifted one as $C1..$DA. So everything typed came out uppercase, and
; shifted letters were dropped entirely by the printable test in grid_key --
; there was no way to enter a lowercase letter at all.
;
; Sending $0F through BSOUT puts the KERNAL in ISO mode proper, and then, in
; the words of the Editor chapter, "the keyboard driver will return
; ASCII/ISO-8859-15 codes": lowercase unshifted, uppercase shifted, matching
; the glyphs already on screen. Control and function keys are outside the
; encoded ranges and come back unchanged.
.proc   _screen_set_iso_charset
        lda     #$0F            ; enable ISO mode: keyboard returns ASCII
        jsr     bsout
        lda     #1              ; 1 = ISO glyphs
        jmp     screen_set_charset
.endproc

; uint16_t screen_get_size(void);
;
; screen_mode with carry set reports the current text size in .X (columns)
; and .Y (rows). cc65 returns a 16-bit value in .A low / .X high, so the
; caller unpacks columns from the low byte and rows from the high byte.
.proc   _screen_get_size
        sec
        jsr     screen_mode
        txa                     ; .A = columns
        pha
        tya                     ; .X = rows
        tax
        pla
        rts
.endproc
