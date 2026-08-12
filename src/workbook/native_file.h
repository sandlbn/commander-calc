/* native_file.h — the .X16S workbook format.
 *
 * Chunked rather than a table of fixed offsets, so a reader can skip a
 * section it does not recognise instead of refusing the file. That is what
 * lets version 1 open a file written by a later version that added, say,
 * charts — it ignores the chunk and reports what it dropped.
 *
 *   "X16S"  magic
 *   u16     version
 *   u16     flags
 *   chunks: u8 tag[4], u32 length, payload
 *   "END "  length 0
 *   u32     CRC-32 of every byte before this field
 *
 * All integers are little-endian and fixed width.
 *
 * These live in OVL_FILEIO and the dialogs that choose a name live in
 * OVL_FILEDLG, so one file operation loads two overlays in turn.
 */
#ifndef X16S_NATIVE_FILE_H
#define X16S_NATIVE_FILE_H

#include "../util/errors.h"

/* 2: every worksheet, not just the active one.
 *
 * A version 1 file is one unnamed sheet and still opens -- the reader keeps
 * that path. A version 2 file will not open in an older build, which is
 * what the version field is for: it refuses rather than reading the first
 * sheet and silently dropping the rest. */
/* 3: a formula cell carries the pool id of its source text. Versions 1 and
 * 2 wrote the banked handle instead, which meant nothing after a reload --
 * every saved formula came back as a nonsense number. Those files still
 * open; their formula cells are dropped, because there is nothing left to
 * rebuild them from. */
#define X16S_VERSION 3
#define X16S_TMP     "CMDRCALC.TMP"

/* Save atomically: write a temporary file, verify it reads back with the
 * right checksum, and only then replace the target. A power cut or a full
 * card leaves the previous workbook intact rather than a half-written one. */
err_t x16s_save(const char *name);

/* Replaces the current workbook entirely. On a damaged file the workbook is
 * left empty rather than half-loaded. */
err_t x16s_load(const char *name);

/* Read one, and compile the formulas it carried. THE ONE TO CALL: see
 * workbook.c. x16s_load() above is the half that runs in OVL_FILEIO and
 * cannot finish the job itself. */
err_t x16s_open(const char *name);

#endif /* X16S_NATIVE_FILE_H */
