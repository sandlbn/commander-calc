#define FILEIO_OWNER_ZIP
#include "../platform/file_io_ovl.h"

#include "zip.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY6")
#  pragma rodata-name (push, "OVL6RO")
#endif

/* Signatures, little-endian on disk. */
#define SIG_LOCAL   0x04034B50UL
#define SIG_CENTRAL 0x02014B50UL
#define SIG_EOCD    0x06054B50UL

#define LOCAL_FIXED   30        /* bytes before the name */
#define CENTRAL_FIXED 46
#define EOCD_FIXED    22

/* How far back to look for the end-of-central-directory record. It sits at
 * the very end unless the archive has a trailing comment, which spreadsheet
 * writers do not produce; 512 bytes covers a short one without reading the
 * whole file backwards.
 *
 * The search walks that range in windows rather than reading it all at once,
 * because cc65 caps a function's locals near 256 bytes. Consecutive windows
 * overlap by one record length so a signature straddling the boundary is
 * still seen. */
#define EOCD_SCAN 512
#define EOCD_WIN   96

/* --- little-endian scalars out of a byte buffer --------------------- */

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Read exactly `len` bytes, or fail. Short reads are the normal way a
 * truncated archive shows up, so they must not be mistaken for data. */
static err_t read_exact(zip_t *z, void *buf, uint16_t len)
{
    uint8_t *p = (uint8_t *)buf;
    uint16_t got;

    while (len) {
        got = file_read(&z->f, p, len);
        if (got == 0)
            return ERR_EOF;
        p += got;
        len = (uint16_t)(len - got);
    }
    return ERR_OK;
}

static err_t seek_read(zip_t *z, uint32_t pos, void *buf, uint16_t len)
{
    err_t e = file_seek(&z->f, pos);

    if (e != ERR_OK)
        return e;
    return read_exact(z, buf, len);
}

/* --- opening the archive -------------------------------------------- */

err_t zip_open(zip_t *z, const char *filename)
{
    uint8_t  buf[EOCD_WIN];
    uint32_t size, pos, floor_, start;
    uint16_t n, i;
    err_t e;

    memset(z, 0, sizeof *z);

    e = file_open_read(&z->f, filename);
    if (e != ERR_OK)
        return e;

    e = file_size(&z->f, &size);
    if (e != ERR_OK) {
        file_close(&z->f);
        return e;
    }
    if (size < EOCD_FIXED) {
        file_close(&z->f);
        return ERR_BADFORMAT;
    }

    /* Walk backwards from the end. Backwards matters: the bytes of a
     * compressed entry can happen to contain the EOCD signature, and the
     * real record is always the last one. */
    floor_ = (size > EOCD_SCAN) ? (uint32_t)(size - EOCD_SCAN) : 0;
    pos = size;

    while (pos > floor_) {
        start = (pos >= EOCD_WIN) ? (uint32_t)(pos - EOCD_WIN) : 0;
        if (start < floor_)
            start = floor_;
        n = (uint16_t)(pos - start);

        e = seek_read(z, start, buf, n);
        if (e != ERR_OK)
            break;

        if (n >= EOCD_FIXED) {
            for (i = (uint16_t)(n - EOCD_FIXED + 1); i > 0; --i) {
                const uint8_t *p = buf + (i - 1);
                if (le32(p) != SIG_EOCD)
                    continue;
                z->entry_count = le16(p + 10);
                z->cd_offset   = le32(p + 16);
                z->cd_pos      = z->cd_offset;
                if (z->cd_offset >= size) {
                    file_close(&z->f);
                    return ERR_BADFORMAT;
                }
                return ERR_OK;
            }
        }

        if (start == floor_)
            break;
        pos = start + (EOCD_FIXED - 1);     /* overlap the next window */
    }

    file_close(&z->f);
    return ERR_BADFORMAT;
}

void zip_close(zip_t *z)
{
    file_close(&z->f);
    memset(z, 0, sizeof *z);
}

/* --- walking the central directory ----------------------------------- */

err_t zip_first(zip_t *z, zip_entry_t *e)
{
    z->cd_pos = z->cd_offset;
    z->entry_index = 0;
    return zip_next(z, e);
}

err_t zip_next(zip_t *z, zip_entry_t *e)
{
    uint8_t hdr[CENTRAL_FIXED];
    uint16_t name_len, extra_len, comment_len, keep;
    err_t err;

    if (z->entry_index >= z->entry_count)
        return ERR_EOF;

    err = seek_read(z, z->cd_pos, hdr, CENTRAL_FIXED);
    if (err != ERR_OK)
        return err;
    if (le32(hdr) != SIG_CENTRAL)
        return ERR_BADFORMAT;

    memset(e, 0, sizeof *e);
    e->method       = le16(hdr + 10);
    e->crc          = le32(hdr + 16);
    e->comp_size    = le32(hdr + 20);
    e->uncomp_size  = le32(hdr + 24);
    name_len        = le16(hdr + 28);
    extra_len       = le16(hdr + 30);
    comment_len     = le16(hdr + 32);
    e->local_offset = le32(hdr + 42);

    /* A name longer than we can hold is kept truncated rather than refused:
     * it will simply never match a lookup, and an archive with one long
     * name should still list the others. */
    keep = name_len < ZIP_NAME_MAX ? name_len : (uint16_t)(ZIP_NAME_MAX - 1);
    err = read_exact(z, e->name, keep);
    if (err != ERR_OK)
        return err;
    e->name[keep] = '\0';

    z->cd_pos = z->cd_pos + CENTRAL_FIXED + name_len + extra_len + comment_len;
    ++z->entry_index;
    return ERR_OK;
}

err_t zip_find(zip_t *z, const char *path, zip_entry_t *e)
{
    err_t err = zip_first(z, e);

    while (err == ERR_OK) {
        if (strcmp(e->name, path) == 0)
            return ERR_OK;
        err = zip_next(z, e);
    }
    return err == ERR_EOF ? ERR_NOTFOUND : err;
}

/* --- reading one entry ------------------------------------------------ */

err_t zip_open_entry(zip_t *z, const zip_entry_t *e)
{
    uint8_t hdr[LOCAL_FIXED];
    err_t err;

    err = seek_read(z, e->local_offset, hdr, LOCAL_FIXED);
    if (err != ERR_OK)
        return err;
    if (le32(hdr) != SIG_LOCAL)
        return ERR_BADFORMAT;

    /* Only the two variable-length fields are taken from the local header.
     * Its sizes are not trustworthy — a streaming writer sets bit 3 of the
     * flags and leaves them zero, putting the real values in a descriptor
     * after the data. The central directory always has them. */
    z->data_pos  = e->local_offset + LOCAL_FIXED
                 + le16(hdr + 26) + le16(hdr + 28);
    z->remaining = e->comp_size;
    z->open_entry = 1;

    return file_seek(&z->f, z->data_pos);
}

uint16_t zip_read(zip_t *z, void *buf, uint16_t len)
{
    uint16_t got;

    if (!z->open_entry || z->remaining == 0)
        return 0;

    /* Never read past this entry into the next one's header. */
    if ((uint32_t)len > z->remaining)
        len = (uint16_t)z->remaining;

    got = file_read(&z->f, buf, len);
    z->remaining -= got;
    return got;
}

#ifdef __CC65__
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
