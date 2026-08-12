;
; dos_x16.s — send a command to CMDR-DOS with an explicit length.
;
; Every cc65 wrapper for SETNAM measures the name with strlen, which is fine
; for a filename and wrong for a DOS command. POSITION is "P" followed by a
; channel byte and a 32-bit offset as raw bytes, and those bytes are
; frequently zero — so a strlen-based SETNAM truncates the command at the
; first zero byte and seeks somewhere else entirely. Hence this.
;
; Opens the command channel with the command as its "filename", which is how
; CBM DOS has always taken commands. The caller reads the response and then
; closes the channel: closing here would discard the reply.
;
; uint8_t __fastcall__ dos_command(const char *cmd, uint8_t len, uint8_t dev);
;   0 on success, otherwise the KERNAL error code.
;

; In OVL_ZIP, not resident. Seeking is the only thing that needs a DOS
; command with an explicit length, and the ZIP reader is the only thing
; that seeks -- file_io_x16.c is compiled into several overlays and this is
; the one copy that calls in here. Resident memory is measured in tens of
; bytes; this is forty-five of them.
        .segment "OVERLAY6"

        .export         _dos_command

        .import         popa, popax
        .importzp       ptr1, tmp1, tmp2

SETLFS  = $FFBA
SETNAM  = $FFBD
OPEN    = $FFC0

LFN_CMD = 15
SA_CMD  = 15

.proc   _dos_command
        sta     tmp1            ; device (last argument, in .A)

        jsr     popa
        sta     tmp2            ; length

        jsr     popax           ; .A = pointer low, .X = pointer high
        sta     ptr1
        stx     ptr1+1

        lda     tmp2            ; SETNAM: .A = length, .X/.Y = pointer
        ldx     ptr1
        ldy     ptr1+1
        jsr     SETNAM

        lda     #LFN_CMD        ; SETLFS: .A = file, .X = device, .Y = sa
        ldx     tmp1
        ldy     #SA_CMD
        jsr     SETLFS

        jsr     OPEN
        bcs     @err

        lda     #$00            ; success
        ldx     #$00
        rts

@err:   ldx     #$00            ; .A already holds the KERNAL error code
        rts
.endproc
