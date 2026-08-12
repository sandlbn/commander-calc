/* file_io_host.c — the SD card, as a directory on the host.
 *
 * Exists so the .X16S reader and writer can be round-tripped against real
 * bytes in the test suite. A format bug found here is a minute; the same bug
 * found on the X16 is a corrupted workbook and a debugger session.
 */
#include "file_io.h"
#include <stdio.h>
#include <string.h>

static char root[512] = ".";

void file_host_set_root(const char *path)
{
    strncpy(root, path, sizeof root - 1);
    root[sizeof root - 1] = '\0';
}

const char *file_host_root(void) { return root; }

static void full_path(char *out, size_t max, const char *name)
{
    snprintf(out, max, "%s/%s", root, name);
}

err_t file_open_read(fstream_t *f, const char *name)
{
    char path[600];

    memset(f, 0, sizeof *f);
    full_path(path, sizeof path, name);
    f->fp = fopen(path, "rb");
    return f->fp ? ERR_OK : ERR_NOTFOUND;
}

err_t file_open_write(fstream_t *f, const char *name)
{
    char path[600];

    memset(f, 0, sizeof *f);
    f->writing = 1;
    full_path(path, sizeof path, name);
    f->fp = fopen(path, "wb");
    return f->fp ? ERR_OK : ERR_IO;
}

uint16_t file_read(fstream_t *f, void *buf, uint16_t len)
{
    size_t got;

    if (!f->fp)
        return 0;
    got = fread(buf, 1, len, (FILE *)f->fp);
    if (got < len)
        f->eof = 1;
    return (uint16_t)got;
}

uint16_t file_write(fstream_t *f, const void *buf, uint16_t len)
{
    if (!f->fp)
        return 0;
    return (uint16_t)fwrite(buf, 1, len, (FILE *)f->fp);
}

uint8_t file_eof(const fstream_t *f) { return f->eof; }

err_t file_close(fstream_t *f)
{
    int rc = 0;

    if (f->fp)
        rc = fclose((FILE *)f->fp);
    f->fp = 0;
    return rc == 0 ? ERR_OK : ERR_IO;
}

err_t file_seek(fstream_t *f, uint32_t pos)
{
    if (!f->fp)
        return ERR_IO;
    if (fseek((FILE *)f->fp, (long)pos, SEEK_SET) != 0)
        return ERR_IO;
    f->eof = 0;
    return ERR_OK;
}

err_t file_size(fstream_t *f, uint32_t *out)
{
    long here, end;

    if (!f->fp)
        return ERR_IO;
    here = ftell((FILE *)f->fp);
    if (fseek((FILE *)f->fp, 0, SEEK_END) != 0)
        return ERR_IO;
    end = ftell((FILE *)f->fp);
    fseek((FILE *)f->fp, here, SEEK_SET);
    *out = (uint32_t)end;
    return ERR_OK;
}

err_t file_remove(const char *name)
{
    char path[600];

    full_path(path, sizeof path, name);
    return remove(path) == 0 ? ERR_OK : ERR_NOTFOUND;
}

err_t file_rename(const char *from, const char *to)
{
    char a[600], b[600];

    full_path(a, sizeof a, from);
    full_path(b, sizeof b, to);
    return rename(a, b) == 0 ? ERR_OK : ERR_IO;
}
