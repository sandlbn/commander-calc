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
            c = (unsigned char)(c - 0xC1 + 'a');
        dst[i] = (char)c;
        ++i;
    }
    dst[i] = '\0';
}

/* Golden RAM: this file's pragma block names an overlay for code and
 * rodata but not for bss, so this one byte was resident. */
X16S_GOLDEN_BEGIN
static uint8_t is_open;
X16S_GOLDEN_END

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
