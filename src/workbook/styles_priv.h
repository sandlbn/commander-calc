/* styles_priv.h — the style table's insides, for the loader only.
 *
 * styles_append() adds an entry without de-duplicating it. Only the .X16S
 * reader needs that — styles are written in id order and cells refer to
 * them by index, so collapsing two identical entries on the way in would
 * silently re-point cells at the wrong format. Nothing else may use it.
 *
 * Its one caller is native_file.c, so the code lives in OVL_FILEIO. The
 * STATE stays resident, as in strings_priv.h: a second copy of `table` or
 * `count` in an overlay would be a second style table disagreeing with the
 * first.
 */
#ifndef X16S_STYLES_PRIV_H
#define X16S_STYLES_PRIV_H

#include "styles.h"

extern handle_t sty_table;
extern uint8_t  sty_count;
extern uint8_t  sty_bord;

#endif /* X16S_STYLES_PRIV_H */
