/* file_io.h — byte streams on the SD card.
 *
 * Resident rather than overlaid: save/load, CSV and the ZIP reader all need
 * it, and one copy costs less than a copy inside each overlay.
 *
 * The host backend is stdio over a directory, so the .X16S writer and reader
 * are exercised against real files in the test suite — which is the only
 * practical way to know a format round-trips before trusting a workbook to
 * it.
 *
 * Filenames are ASCII here. The X16 backend converts to PETSCII on the way
 * to the KERNAL and back on the way out of a directory listing, so nothing
 * above this line has to think about encodings.
 */
#ifndef X16S_FILE_IO_H
#define X16S_FILE_IO_H

#include "../util/errors.h"

#define FILE_NAME_MAX 24        /* CMDR-DOS allows 16; leave room for ",S,W" */

typedef struct {
    uint8_t lfn;
    uint8_t dev;
    uint8_t eof;
    uint8_t writing;
#ifdef X16S_HOST
    void   *fp;
#else
    /* The open name, already in PETSCII. Kept so a seek can reopen the
     * channel: the KERNAL latches end-of-file per channel, and reopening is
     * the only way to clear it. See file_seek. */
    char    petname[FILE_NAME_MAX + 8];
#endif
} fstream_t;

void    file_set_device(uint8_t dev);
uint8_t file_device(void);

err_t    file_open_read(fstream_t *f, const char *name);
err_t    file_open_write(fstream_t *f, const char *name);
uint16_t file_read(fstream_t *f, void *buf, uint16_t len);
uint16_t file_write(fstream_t *f, const void *buf, uint16_t len);
uint8_t  file_eof(const fstream_t *f);
err_t    file_close(fstream_t *f);

/* Byte position within an open file, and the file's length. CMDR-DOS
 * implements both on the command channel: P to position, T to tell.
 *
 * The ZIP reader needs them. A zip's directory is at the *end* of the file,
 * so there is no way to read one as a stream — you seek to the end, find the
 * directory, then seek back to whichever entry you want. */
err_t file_seek(fstream_t *f, uint32_t pos);
err_t file_size(fstream_t *f, uint32_t *out);

err_t file_remove(const char *name);
err_t file_rename(const char *from, const char *to);

/* --- directory listing, for the file dialog --- */
typedef struct {
    char     name[FILE_NAME_MAX];
    uint16_t blocks;
    uint8_t  is_dir;
} file_entry_t;

err_t   dir_open(void);
uint8_t dir_next(file_entry_t *e);      /* 1 while entries remain */
void    dir_close(void);

#ifdef X16S_HOST
/* Point the host backend at a directory to use as the "SD card". */
void file_host_set_root(const char *path);
#endif

#endif /* X16S_FILE_IO_H */
