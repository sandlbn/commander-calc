/* filedir_x16.c — directory listing, in the file-dialog overlay.
 *
 * Split out of file_io_x16.c because cc65's cbm_readdir is 639 bytes — the
 * single largest library module this program pulls in — and the only thing
 * that ever wants it is the Open dialog. Resident memory is the scarcest
 * resource here, so code used by one modal dialog belongs in that dialog's
 * overlay.
 */
#include "file_io.h"
#include <cbm.h>
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY1")
#  pragma rodata-name (push, "OVL1RO")
#endif

#define LFN_DIR 3

/* CMDR-DOS hands names back in PETSCII; the ISO screen font wants ASCII.
 * Uppercase and digits are identical in both, so only the shifted range
 * needs moving. */
static void from_petscii(char *dst, const char *src, uint8_t max)
{
    uint8_t i = 0;

    while (src[i] && i < (uint8_t)(max - 1)) {
        unsigned char c = (unsigned char)src[i];
        if (c >= 0xC1 && c <= 0xDA)
            c = (unsigned char)(c - 0xC1 + 'A');
        dst[i] = (char)c;
        ++i;
    }
    dst[i] = '\0';
}

/* The inverse, for a name being handed back to CMDR-DOS. */
static void to_dosname(char *dst, const char *src, uint8_t max)
{
    uint8_t i = 0;

    while (src[i] && i < (uint8_t)(max - 1)) {
        char c = src[i];
        if (c >= 'a' && c <= 'z')
            c = (char)(c - 'a' + 'A');
        dst[i] = c;
        ++i;
    }
    dst[i] = '\0';
}

/* Golden RAM: this file's pragma block names an overlay for code and
 * rodata but not for bss, so these would be resident.
 *
 * The command buffer is here rather than on the C stack for the same
 * reason. */
X16S_GOLDEN_BEGIN
static uint8_t is_open;
static char    cmd[FILE_NAME_MAX + 4];
X16S_GOLDEN_END

/* Send one command on the DOS command channel and say whether it worked.
 *
 * Open-coded rather than calling file_io_x16.c's send_command(): that lives
 * in another overlay, and an overlay cannot call another overlay. The status
 * must be read even though only its first digit is wanted, or the next
 * command reports this one's result. */
/* The DOS's reply to the last command, as its two digits, or 0xEE if the
 * command channel could not be read at all. Reported by the dialog: a
 * directory that will not open otherwise looks exactly like a key that did
 * nothing, which is the one thing worse than an error message. */
uint8_t dir_status;

static err_t dos_cmd(const char *c)
{
    err_t e = ERR_IO;

    dir_status = 0xEE;
    if (cbm_open(15, file_device(), 15, c) != 0)
        return ERR_IO;
    if (cbm_k_chkin(15) == 0) {
        uint8_t hi = cbm_k_basin();
        uint8_t lo = cbm_k_basin();

        dir_status = (uint8_t)(((hi - '0') << 4) | ((lo - '0') & 15));
        if (hi == '0' && lo == '0')     /* "00, OK" */
            e = ERR_OK;
        while (!cbm_k_readst())
            cbm_k_basin();
        cbm_k_clrch();
    }
    cbm_close(15);
    return e;
}

/* Change directory, trying both spellings of the name.
 *
 * from_petscii() is not reversible, and that is the whole reason this is
 * two attempts rather than one. It maps $C1-$DA down to 'a'-'z' and leaves
 * everything else, so a name delivered as PLAIN ASCII lowercase comes back
 * out shifted by to_dosname() and names a different file: "obj" is asked
 * for as "OBJ", and the DOS answers 62.
 *
 * Real CMDR-DOS hands back shifted PETSCII, where the round trip is exact.
 * The emulator's HostFS hands back the host's own bytes, where it is not.
 * Rather than guess which machine this is, ask for the converted form and
 * fall back to the bytes exactly as the listing delivered them. */
err_t dir_chdir(const char *name)
{
    err_t e;

    cmd[0] = 'C';                       /* uppercase: ASCII and PETSCII */
    cmd[1] = 'D';                       /* agree there, so no translation */
    cmd[2] = ':';

    to_dosname(cmd + 3, name, (uint8_t)(sizeof cmd - 3));
    e = dos_cmd(cmd);
    if (e == ERR_OK)
        return e;

    {
        uint8_t i = 0;

        while (name[i] && i < (uint8_t)(sizeof cmd - 4)) {
            cmd[3 + i] = name[i];
            ++i;
        }
        cmd[3 + i] = '\0';
    }
    return dos_cmd(cmd);
}

err_t dir_open(void)
{
    if (cbm_opendir(LFN_DIR, file_device()) != 0)
        return ERR_IO;
    is_open = 1;
    return ERR_OK;
}

uint8_t dir_next(file_entry_t *e)
{
    struct cbm_dirent d;

    if (!is_open)
        return 0;
    if (cbm_readdir(LFN_DIR, &d) != 0)
        return 0;

    from_petscii(e->name, d.name, FILE_NAME_MAX);
    e->blocks = d.size;
    e->is_dir = (d.type == CBM_T_DIR);
    return 1;
}

void dir_close(void)
{
    if (is_open) {
        cbm_closedir(LFN_DIR);
        is_open = 0;
    }
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
