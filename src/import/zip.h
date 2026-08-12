/* zip.h — reading entries out of a ZIP archive.
 *
 * An .xlsx is a ZIP of XML documents, so this is the bottom of the import
 * pipeline. Nothing is ever loaded whole: the caller asks for one entry by
 * name and reads it as a stream.
 *
 * Layout of the format, in the order this code touches it:
 *
 *   [local header][data] [local header][data] ... [central directory][EOCD]
 *
 * The end-of-central-directory record is at the *end* of the file, and it is
 * what says where the directory begins. That is why this needs file_seek and
 * cannot work on a pure stream — a fact worth knowing before wondering why
 * the ZIP reader is not simply another parser.
 *
 * The central directory is authoritative. Local headers are read only for
 * their variable-length fields, because a zip written by a streaming writer
 * leaves the sizes in the local header as zero and puts the real ones in a
 * trailing data descriptor.
 *
 * Lives in OVL_ZIP together with the inflate decoder and its own copy of the
 * file layer.
 */
#ifndef X16S_ZIP_H
#define X16S_ZIP_H

#include "../platform/file_io.h"

#define ZIP_NAME_MAX 48         /* "xl/worksheets/sheet1.xml" and friends */

#define ZIP_STORED  0
#define ZIP_DEFLATE 8

typedef struct {
    char     name[ZIP_NAME_MAX];
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint32_t local_offset;      /* of the local header, not the data */
    uint32_t crc;
    uint16_t method;
} zip_entry_t;

typedef struct {
    fstream_t f;
    uint32_t  cd_offset;        /* start of the central directory      */
    uint32_t  cd_pos;           /* iteration cursor within it          */
    uint16_t  entry_count;
    uint16_t  entry_index;
    /* set while an entry is open */
    uint32_t  data_pos;         /* where its bytes start               */
    uint32_t  remaining;        /* compressed bytes still unread       */
    uint8_t   open_entry;
} zip_t;

err_t zip_open(zip_t *z, const char *filename);
void  zip_close(zip_t *z);

/* Walk the central directory. Returns ERR_EOF when there are no more. */
err_t zip_first(zip_t *z, zip_entry_t *e);
err_t zip_next(zip_t *z, zip_entry_t *e);

/* Find one entry by its full path inside the archive, e.g.
 * "xl/worksheets/sheet1.xml". Case-sensitive, as the format is. */
err_t zip_find(zip_t *z, const char *path, zip_entry_t *e);

/* Position at an entry's data. After this, zip_read returns its bytes —
 * still compressed if the entry is deflated. */
err_t zip_open_entry(zip_t *z, const zip_entry_t *e);

/* Read raw (possibly compressed) bytes. Returns 0 at the end of the entry,
 * and never reads past it into the next one. */
uint16_t zip_read(zip_t *z, void *buf, uint16_t len);

#endif /* X16S_ZIP_H */
