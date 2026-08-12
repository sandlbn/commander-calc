/* errors.h — one error space for the whole application.
 *
 * Codes are stable across versions: they appear in .X16S files (as cell
 * error values) and in import logs. Never renumber, only append.
 */
#ifndef X16S_ERRORS_H
#define X16S_ERRORS_H

#include "../x16sheet.h"

typedef enum {
    ERR_OK = 0,

    /* memory */
    ERR_NOMEM = 10,          /* banked allocator exhausted              */
    ERR_LIMIT,               /* a documented resource limit was reached */

    /* file / stream */
    ERR_IO = 20,             /* device error                            */
    ERR_NOTFOUND,
    ERR_EOF,
    ERR_BADFORMAT,           /* magic/version/structure wrong           */
    ERR_CHECKSUM,

    /* formula */
    ERR_SYNTAX = 40,
    ERR_NAME,                /* #NAME?                                  */
    ERR_REF,                 /* #REF!                                   */
    ERR_VALUE,               /* #VALUE!                                 */
    ERR_DIVZERO,             /* #DIV/0!                                 */
    ERR_NUM,                 /* #NUM!                                   */
    ERR_CYCLE,               /* #CYCLE!                                 */

    /* import */
    ERR_UNSUPPORTED = 60,    /* recognised but deliberately not handled */
    ERR_TRUNCATED            /* import stopped at a limit               */
} err_t;

/* Short, user-facing text. Never NULL. */
const char *err_message(err_t e);

/* The '#NAME?'-style spreadsheet display string for the formula errors,
 * or NULL if this code has no cell representation. */
const char *err_cell_text(err_t e);

#endif /* X16S_ERRORS_H */
