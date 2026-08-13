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

/* Where dir_chdir() has taken us, relative to the root, so dir_next() can
 * stat entries inside it. The X16 changes the machine's own directory; the
 * host keeps its own so a test cannot disturb the process. */
static char cwd[X16S_PATH_MAX];
static char here[600];

uint8_t dir_status;

err_t dir_chdir(const char *name)
{
    struct stat st;
    size_t n = strlen(cwd);

    dir_status = 0;

    if (name[0] == '.' && name[1] == '.') {
        while (n && cwd[n - 1] != '/')
            --n;
        if (n)
            --n;
        cwd[n] = '\0';
        return ERR_OK;
    }

    /* Refuse anything that could climb out of the root. The X16 cannot
     * express one -- the dialog only ever descends into a name it listed --
     * but the host is a real filesystem and a bad path should fail rather
     * than wander off into the build tree. */
    if (name[0] == '/' || strchr(name, '.') == name)
        return ERR_IO;
    if (n + strlen(name) + 2 >= sizeof cwd)
        return ERR_IO;

    if (n)
        cwd[n++] = '/';
    strcpy(cwd + n, name);

    snprintf(here, sizeof here, "%s/%s", file_host_root(), cwd);
    if (stat(here, &st) != 0 || !S_ISDIR(st.st_mode)) {
        cwd[n ? n - 1 : 0] = '\0';      /* put it back */
        return ERR_IO;
    }
    return ERR_OK;
}

err_t dir_open(void)
{
    dir_close();
    if (cwd[0])
        snprintf(here, sizeof here, "%s/%s", file_host_root(), cwd);
    else
        snprintf(here, sizeof here, "%s", file_host_root());
    dh = opendir(here);
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
        /* Big enough for `here` plus a separator and the longest name, so
         * the entry being stat'ed is never the truncation of a longer one. */
        char path[sizeof here + FILE_NAME_MAX + 1];
        struct stat st;

        snprintf(path, sizeof path, "%s/%s", here, e->name);
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
