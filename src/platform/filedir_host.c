/* filedir_host.c — the host half of the directory listing.
 * Separate from file_io_host.c to mirror the X16 split, where these live in
 * the file-dialog overlay rather than in resident memory.
 */
#include "file_io.h"
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdio.h>

/* The root is owned by file_io_host.c; this asks for it rather than
 * keeping a second copy that could drift out of step. */
extern const char *file_host_root(void);

static DIR *dh;

err_t dir_open(void)
{
    dir_close();
    dh = opendir(file_host_root());
    return dh ? ERR_OK : ERR_IO;
}

uint8_t dir_next(file_entry_t *e)
{
    struct dirent *d;

    if (!dh)
        return 0;
    for (;;) {
        d = readdir(dh);
        if (!d)
            return 0;
        if (d->d_name[0] == '.')
            continue;                    /* CMDR-DOS shows no dot entries */
        break;
    }
    strncpy(e->name, d->d_name, FILE_NAME_MAX - 1);
    e->name[FILE_NAME_MAX - 1] = '\0';
    {
        char path[600];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", file_host_root(), e->name);
        e->is_dir = (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
        e->blocks = (uint16_t)((stat(path, &st) == 0)
                               ? (st.st_size + 253) / 254 : 0);
    }
    return 1;
}

void dir_close(void)
{
    if (dh) {
        closedir(dh);
        dh = 0;
    }
}
