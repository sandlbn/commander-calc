/* zipw.c — the ZIP writer. See zipw.h for why nothing here compresses.
 *
 * #included by xlsx_export.c rather than compiled on its own, and every
 * function here is static as a result. The exporter is built twice, once
 * per overlay, and two objects exporting zipw_add would not link; making
 * the whole of it internal to its includer is cheaper than a rename header
 * of the sort blob_ovl.h and file_io_ovl.h need. It has no other caller.
 */
/* The includer has already set the owner macros and pulled in zipw.h,
 * crc32 and string.h. */

/* One buffer for the header being assembled. 46 bytes is the longest fixed
 * part of any ZIP record (the central directory entry); the name follows it
 * and is written separately. */
static uint8_t hdr[46];

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

/* Every write goes through here, so `pos` cannot drift out of step with the
 * file and one error check covers the whole archive. */
static void zw_put(zipw_t *z, const void *p, uint16_t len)
{
    if (z->err != ERR_OK)
        return;
    if (file_write(&z->f, p, len) != len) {
        z->err = ERR_IO;
        return;
    }
    z->pos += len;
}

static err_t zipw_open(zipw_t *z, const char *filename)
{
    memset(z, 0, sizeof *z);
    z->cd = bank_alloc(ZIPW_CD_BYTES);
    if (z->cd == H_NULL)
        return ERR_NOMEM;
    return file_open_write(&z->f, filename);
}

static err_t zipw_begin(zipw_t *z, const char *name,
                        uint32_t size, uint32_t crc)
{
    uint32_t at = z->pos;
    uint16_t n;

    if (z->err != ERR_OK)
        return z->err;

    n = (uint16_t)strlen(name);
    if (z->cd_len + 46 + n > ZIPW_CD_BYTES)
        return (z->err = ERR_LIMIT);
    put32(hdr + 0, 0x04034B50UL);       /* local file header signature   */
    put16(hdr + 4, 20);                 /* version needed: 2.0           */
    put16(hdr + 6, 0);                  /* no flags: no data descriptor  */
    put16(hdr + 8, ZIPW_STORED);
    put16(hdr + 10, 0);                 /* modification time             */
    put16(hdr + 12, 0);                 /* modification date             */
    put32(hdr + 14, crc);
    put32(hdr + 18, size);
    put32(hdr + 22, size);
    put16(hdr + 26, n);
    put16(hdr + 28, 0);                 /* no extra field                */
    zw_put(z, hdr, 30);
    zw_put(z, name, n);

    /* And this entry's directory record, built now and kept in banked RAM
     * until the end: everything it needs is already known.
     *
     * The two records agree on 26 bytes -- version-needed through to the
     * extra-field length -- shifted by two, because the directory record
     * has an extra "version made by" in front. Moving them beats setting
     * each one twice. */
    memmove(hdr + 6, hdr + 4, 26);
    put32(hdr + 0, 0x02014B50UL);       /* central directory signature   */
    put16(hdr + 4, 20);                 /* version made by              */
    memset(hdr + 32, 0, 10);            /* comment, disk, attributes    */
    put32(hdr + 42, at);                /* where its local header is    */
    bank_write(z->cd, z->cd_len, hdr, 46);
    z->cd_len = (uint16_t)(z->cd_len + 46);
    bank_write(z->cd, z->cd_len, name, n);
    z->cd_len = (uint16_t)(z->cd_len + n);

    z->left = size;
    return z->err;
}

static void zipw_data(zipw_t *z, const void *p, uint16_t len)
{
    if (len > z->left) {
        /* The measuring pass said less than the writing pass produced. Stop
         * rather than run past the length already in the header. */
        z->err = ERR_BADFORMAT;
        return;
    }
    zw_put(z, p, len);
    z->left -= len;
}

static err_t zipw_end(zipw_t *z)
{
    if (z->err == ERR_OK && z->left != 0)
        z->err = ERR_BADFORMAT;         /* promised more than it wrote */
    if (z->err == ERR_OK)
        ++z->count;
    return z->err;
}

static err_t zipw_finish(zipw_t *z)
{
    uint32_t cd_start = z->pos;
    uint8_t  buf[64];
    uint16_t off = 0;

    /* The directory was built as the entries were written; all that is
     * left is to put it on the end. */
    while (off < z->cd_len && z->err == ERR_OK) {
        uint16_t n = (uint16_t)(z->cd_len - off);
        if (n > sizeof buf)
            n = sizeof buf;
        bank_read(z->cd, off, buf, n);
        zw_put(z, buf, n);
        off = (uint16_t)(off + n);
    }

    put32(hdr + 0, 0x06054B50UL);       /* end of central directory      */
    put16(hdr + 4, 0);                  /* this disk                    */
    put16(hdr + 6, 0);                  /* disk with the directory      */
    put16(hdr + 8, z->count);
    put16(hdr + 10, z->count);
    put32(hdr + 12, z->cd_len);
    put32(hdr + 16, cd_start);
    put16(hdr + 20, 0);                 /* comment length               */
    zw_put(z, hdr, 22);

    file_close(&z->f);
    bank_free(z->cd);
    z->cd = H_NULL;
    return z->err;
}
