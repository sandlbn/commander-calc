/* KERNAL file streams: open, sequential read and write, close, and seek.
 *
 * Not resident. An overlay cannot call another overlay, so every overlay
 * that touches the card compiles its own copy of this file under a
 * different set of names -- which is why the overlays are named for I/O
 * rather than for formats. See docs/design/files.md.
 */
#include "file_io.h"
#include <cbm.h>
#include <string.h>

#ifdef FILEIO_WANT_SEEK
/* dos_x16.s — SETNAM with an explicit length, for commands containing zero
 * bytes. Resident, so every overlay's copy of this file shares it. */
extern uint8_t __fastcall__ dos_command(const char *cmd, uint8_t len,
                                        uint8_t dev);
#endif

#ifdef __CC65__
#  ifndef FILEIO_NO_SEGMENT
#    pragma code-name (push, "OVERLAY2")
#    pragma rodata-name (push, "OVL2RO")
#    define FILEIO_POP_SEGMENT
#  endif
#endif

#define LFN_DATA 2
#define LFN_CMD  15
#define SA_CMD   15

/* The device number stays resident: the directory listing lives in a
 * different overlay and must be able to ask for it. */
#define device file_device()

/* Filenames cross into the KERNAL as PETSCII. With src/charmap.h in force
 * our literals are plain ASCII, and the two encodings agree on digits,
 * punctuation and uppercase A-Z — so only lowercase needs moving, into the
 * PETSCII shifted range where CMDR-DOS expects it. */
/* ASCII to PETSCII, EXCEPT for a name carrying a path.
 *
 * The mapping exists for names the user types, which arrive as ASCII and
 * have to reach the card as PETSCII. A name with a '/' in it did not come
 * from the keyboard -- it came out of the directory listing, already in
 * whatever encoding the card handed over, and converting it a second time
 * changes it. On the emulator's host filesystem a directory called "obj"
 * goes out as $CF $C2 $CA, which reads back as "OBJ": a different directory
 * on anything that tells the two apart, so the file is not found.
 *
 * The dialog cannot pre-compensate for this, because the bytes it would
 * have to send are exactly the ones this would convert.
 *
 * FILEIO_NO_PATHS drops the test in the copies that can never be handed
 * one -- the .xlsx writer takes its name from a prompt, never from the
 * listing -- because those two overlays have no room for it. */
static void to_petscii(char *dst, const char *src, uint8_t max)
{
    uint8_t i = 0;
#ifndef FILEIO_NO_PATHS
    uint8_t conv = 1;

    while (src[i])
        if (src[i++] == '/') {
            conv = 0;
            break;
        }
    i = 0;
#endif

    while (src[i] && i < (uint8_t)(max - 1)) {
        char c = src[i];
#ifdef FILEIO_NO_PATHS
        if (c >= 'a' && c <= 'z')
#else
        if (conv && c >= 'a' && c <= 'z')
#endif
            c = (char)(c - 'a' + 0xC1);
        dst[i] = c;
        ++i;
    }
    dst[i] = '\0';
}

/* Read the DOS command channel. Anything but code 0 is a failure, and the
 * code is what distinguishes "not found" from a real device problem.
 *
 * The CHKIN is not optional. Without it BASIN reads from the *screen
 * editor* rather than the channel — "calling BASIN for the first time will
 * invoke the KERNAL screen editor", x16-docs chapter 5 — which blocks
 * waiting for the user to type a line into a program that is drawing its
 * own display. */
/* Drain the command channel into `out`, assuming it is already open, and
 * turn the leading two-digit code into an err_t. The channel is closed here
 * either way. */
static err_t drain_status(char *out, uint8_t max)
{
    uint8_t n = 0;
    uint16_t code;

    if (cbm_k_chkin(LFN_CMD) != 0) {
        cbm_close(LFN_CMD);
        return ERR_IO;
    }
    while (n < (uint8_t)(max - 1)) {
        int c = cbm_k_basin();
        if (cbm_k_readst() || c == 13 || c == 0)
            break;
        out[n++] = (char)c;
    }
    out[n] = '\0';
    cbm_k_clrch();
    cbm_close(LFN_CMD);

    if (n < 2)
        return ERR_IO;
    code = (uint16_t)((out[0] - '0') * 10 + (out[1] - '0'));
    if (code == 0)
        return ERR_OK;
    if (code == 62)
        return ERR_NOTFOUND;
    return ERR_IO;
}

static err_t read_status(void)
{
    char buf[40];

    if (cbm_open(LFN_CMD, device, SA_CMD, "") != 0)
        return ERR_IO;
    return drain_status(buf, sizeof buf);
}

/* Overlays that only stream a file never rename or delete one, and the
 * command plumbing is dead weight in their copy. */
#ifndef FILEIO_STREAM_ONLY

static err_t send_command(const char *cmd)
{
    char pet[64];

    to_petscii(pet, cmd, sizeof pet);
    if (cbm_open(LFN_CMD, device, SA_CMD, pet) != 0)
        return ERR_IO;
    cbm_close(LFN_CMD);
    return read_status();
}

#endif /* !FILEIO_STREAM_ONLY */

err_t file_open_read(fstream_t *f, const char *name)
{
    char pet[FILE_NAME_MAX + 8];

    memset(f, 0, sizeof *f);
    f->lfn = LFN_DATA;
    f->dev = device;

    /* The mode has to be spelled out: a bare name on secondary address 2
     * defaults to a PRG file, not the sequential one we wrote. */
    {
        char full[FILE_NAME_MAX + 8];
        uint8_t n = 0;
        while (*name && n < FILE_NAME_MAX)
            full[n++] = *name++;
        full[n++] = ',';
        full[n++] = 'S';
        full[n++] = ',';
        full[n++] = 'R';
        full[n] = '\0';
        to_petscii(pet, full, sizeof pet);
    }
#ifdef FILEIO_WANT_SEEK
    {   /* Remember it: a backward seek has to reopen the channel. Only the
         * seeking copy pays for this. */
        uint8_t k;
        for (k = 0; k < sizeof f->petname - 1 && pet[k]; ++k)
            f->petname[k] = pet[k];
        f->petname[k] = '\0';
    }
#endif
    if (cbm_open(LFN_DATA, device, CBM_SEQ, pet) != 0)
        return ERR_IO;

    /* OPEN succeeds even for a missing file; the command channel is what
     * actually reports 62, FILE NOT FOUND. */
    {
        err_t e = read_status();
        if (e != ERR_OK) {
            cbm_close(LFN_DATA);
            return e;
        }
    }
    return ERR_OK;
}

err_t file_open_write(fstream_t *f, const char *name)
{
    char pet[FILE_NAME_MAX + 8];
    char full[FILE_NAME_MAX + 8];
    uint8_t n = 0;

    memset(f, 0, sizeof *f);
    f->lfn = LFN_DATA;
    f->dev = device;
    f->writing = 1;

    /* "@0:" asks CMDR-DOS to replace an existing file rather than fail. */
    full[n++] = '@';
    full[n++] = '0';
    full[n++] = ':';
    while (*name && n < FILE_NAME_MAX)
        full[n++] = *name++;
    full[n++] = ',';
    full[n++] = 'S';
    full[n++] = ',';
    full[n++] = 'W';
    full[n] = '\0';

    to_petscii(pet, full, sizeof pet);
    if (cbm_open(LFN_DATA, device, CBM_SEQ, pet) != 0)
        return ERR_IO;
    return read_status();
}

uint16_t file_read(fstream_t *f, void *buf, uint16_t len)
{
    int got = cbm_read(f->lfn, buf, len);

    if (got <= 0) {
        f->eof = 1;
        return 0;
    }
    if ((uint16_t)got < len)
        f->eof = 1;
    return (uint16_t)got;
}

uint16_t file_write(fstream_t *f, const void *buf, uint16_t len)
{
    int put = cbm_write(f->lfn, buf, len);

    return put < 0 ? 0 : (uint16_t)put;
}

uint8_t file_eof(const fstream_t *f) { return f->eof; }

err_t file_close(fstream_t *f)
{
    cbm_close(f->lfn);
    /* Only a write can fail late — a full card is reported at close. */
    return f->writing ? read_status() : ERR_OK;
}

/* Seeking is wanted only by the ZIP reader; the native format and the CSV
 * importer are strictly sequential. Leaving it out of the copies that never
 * seek saves 580 bytes in each of them. */
#ifdef FILEIO_WANT_SEEK

/* Two hex digits from the TELL reply. */
static uint8_t hex2(const char *p)
{
    uint8_t i, v = 0;

    for (i = 0; i < 2; ++i) {
        char c = p[i];
        v = (uint8_t)(v << 4);
        if (c >= '0' && c <= '9')       v |= (uint8_t)(c - '0');
        else if (c >= 'A' && c <= 'F')  v |= (uint8_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f')  v |= (uint8_t)(c - 'a' + 10);
    }
    return v;
}

err_t file_seek(fstream_t *f, uint32_t pos)
{
    /* POSITION: "P", the channel, then the offset as four raw bytes
     * (x16-docs chapter 13). dos_command exists because those bytes are
     * usually zero and a strlen-based SETNAM would cut the command short. */
    char cmd[6];
    char reply[40];
    err_t e;

    cmd[0] = 'P';
    cmd[1] = (char)CBM_SEQ;             /* the data channel's secondary */
    cmd[2] = (char)(uint8_t)pos;
    cmd[3] = (char)(uint8_t)(pos >> 8);
    cmd[4] = (char)(uint8_t)(pos >> 16);
    cmd[5] = (char)(uint8_t)(pos >> 24);

    /* Two rules here, both explained in docs/design/files.md:
     *
     * The status channel is drained but never interpreted -- a successful
     * POSITION can report the previous command's error, so believing it
     * would reject valid archives. Callers check something stronger.
     *
     * The data channel is reopened on every seek. The KERNAL latches
     * end-of-file per channel, and the latch cannot be predicted from our
     * own eof flag, so it is always cleared rather than tested. */
    cbm_close(f->lfn);
    cbm_k_clrch();
    if (cbm_open(LFN_DATA, device, CBM_SEQ, f->petname) != 0)
        return ERR_IO;
    f->eof = 0;

    cbm_k_clrch();
    cbm_close(LFN_CMD);

    if (dos_command(cmd, 6, device) != 0)
        return ERR_IO;
    drain_status(reply, sizeof reply);
    (void)e;

    f->eof = 0;
    return ERR_OK;
}

err_t file_size(fstream_t *f, uint32_t *out)
{
    /* TELL replies "07,pppppppp ssssssss,00,00" — position then size, both
     * as eight hex digits. */
    char cmd[2];
    char reply[48];
    uint8_t i;

    (void)f;
    cmd[0] = 'T';
    cmd[1] = (char)CBM_SEQ;

    if (dos_command(cmd, 2, device) != 0)
        return ERR_IO;
    /* TELL answers with code 07, which drain_status calls a failure, so the
     * reply is parsed directly rather than through its return value. */
    drain_status(reply, sizeof reply);

    for (i = 0; reply[i] && reply[i] != ' '; ++i)
        ;
    if (reply[i] != ' ')
        return ERR_IO;
    ++i;

    *out = ((uint32_t)hex2(reply + i) << 24)
         | ((uint32_t)hex2(reply + i + 2) << 16)
         | ((uint32_t)hex2(reply + i + 4) << 8)
         |  (uint32_t)hex2(reply + i + 6);
    return ERR_OK;
}

#endif /* FILEIO_WANT_SEEK */

#ifndef FILEIO_STREAM_ONLY

err_t file_remove(const char *name)
{
    char cmd[FILE_NAME_MAX + 4];
    uint8_t n = 0;

    cmd[n++] = 'S';
    cmd[n++] = ':';
    while (*name && n < sizeof cmd - 1)
        cmd[n++] = *name++;
    cmd[n] = '\0';
    return send_command(cmd);
}

err_t file_rename(const char *from, const char *to)
{
    char cmd[FILE_NAME_MAX * 2 + 4];
    uint8_t n = 0;

    cmd[n++] = 'R';
    cmd[n++] = ':';
    while (*to && n < sizeof cmd - 2)
        cmd[n++] = *to++;
    cmd[n++] = '=';
    while (*from && n < sizeof cmd - 1)
        cmd[n++] = *from++;
    cmd[n] = '\0';
    return send_command(cmd);
}

#endif /* !FILEIO_STREAM_ONLY */

#ifdef FILEIO_POP_SEGMENT
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
