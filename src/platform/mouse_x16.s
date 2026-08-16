; mouse_x16.s -- the KERNAL mouse, in about thirty bytes.
;
; See mouse.h for the interface. Assembly rather than C because the whole
; job is two KERNAL calls whose results are already in the registers cc65
; would have to shuffle: mouse_get hands back the buttons in .A and the
; wheel in .X, and writes the position itself.

        .export _mouse_px, _mouse_py, _mouse_btn, _mouse_whl
        .export _mouse_begin, _mouse_poll, _mouse_end

mouse_config = $FF68
mouse_get    = $FF6B
screen_mode  = $FF5F

; The position lives in zero page because mouse_get insists: it is given the
; offset of a zero-page location in .X and fills four consecutive bytes with
; the X and Y positions. Putting the exported variables there rather than
; copying out of a scratch buffer makes the poll two instructions shorter and
; costs four of the ninety-four zero-page bytes the memory map leaves free.
;
; MUST STAY ADJACENT AND IN THIS ORDER. The KERNAL writes X at +0 and Y at
; +2 from the single address it is given, so these are one four-byte object
; that happens to have two names.
        .segment "ZEROPAGE"
_mouse_px:   .res 2
_mouse_py:   .res 2

; Not zero page: these are written by the poll, not by the KERNAL, so there
; is nothing to gain. GOLDENBSS rather than bss because resident bss is the
; binding constraint -- see cfg/x16sheet.cfg -- and two bytes are two bytes.
        .segment "GOLDENBSS"
_mouse_btn:  .res 1
_mouse_whl:  .res 1

        .code

; .A selects the pointer shape; 1 is the default arrow. .X and .Y give the
; screen resolution in 8-pixel units, and that is what the position gets
; clamped to.
;
; THE RESOLUTION HAS TO BE PASSED. Zero for both is documented as "keep the
; current resolution", which sounds like the right thing to ask for and is a
; trap: if nothing has configured the mouse before, the resolution it keeps
; is 0x0, and every position the KERNAL reports is clamped to (0,0). The
; symptom is a pointer that draws and a wheel that works -- the wheel is a
; delta and never touches the position -- while every click reports the top
; left corner. So this asks screen_mode for the real size first, exactly as
; the KERNAL documentation's own example does.
;
; With carry set, screen_mode gets: mode in .A, width in .X and height in .Y,
; both already in the tile units mouse_config wants.
_mouse_begin:
        sec
        jsr screen_mode         ; -> .X = width, .Y = height, in 8px units
        lda #1
        jmp mouse_config        ; tail call: its rts returns to our caller

; void mouse_end(void);
;
; Config 0 turns the pointer off. Without it the sprite is left on screen
; over whatever runs next, and nothing else is going to take it down.
_mouse_end:
        lda #0
        jmp mouse_config

_mouse_poll:
        ldx #_mouse_px          ; zero-page offset of the 4-byte position
        jsr mouse_get           ; fills the position; .A = buttons, .X = wheel
        sta _mouse_btn
        stx _mouse_whl          ; sta above does not touch .X
        rts
