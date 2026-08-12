/* workbook_priv.h — the sheet table's insides, for the admin commands.
 *
 * Adding, renaming and deleting a sheet are reached from the Sheet menu and
 * happen once in a while; they are about 1.2 KB between them and do not
 * belong in resident memory. They live in sheet_admin.c, in the same
 * overlay as the menu that invokes them.
 *
 * The state stays resident — the renderer and the evaluator read the active
 * sheet on every repaint and every recalculation. Only the code moved.
 * Same arrangement as strings_priv.h and styles_priv.h.
 */
#ifndef X16S_WORKBOOK_PRIV_H
#define X16S_WORKBOOK_PRIV_H

#include "workbook.h"
#include "cells.h"

/* Non-default column widths held per sheet.
 *
 * NINE, and nine is the most that fits. The sheet's banked slot puts its
 * name at offset SH_NAME, the state before it is a cellstore_t (12 bytes,
 * not 8) plus a count, and two bytes a column leaves room for nine. Going
 * to eleven overran the name field and every sheet came back nameless
 * from a saved file -- silently, because nothing checks. test_sheets.c
 * now does; see the assertion there.
 *
 * The .X16S format is unaffected whatever this is: its COLW chunk stores
 * all 256 columns either way. So this costs six bytes of golden RAM and
 * nothing on disk.
 *
 * It is still a cap, and the tenth is dropped in silence. */
#define CW_MAX 9

/* Everything about a sheet except its name: written to and read from its
 * banked slot in one transfer. */
typedef struct {
    cellstore_t store;
    uint8_t     cw_n;
    uint8_t     cw_col[CW_MAX];
    uint8_t     cw_val[CW_MAX];
} sheet_state_t;

#define SH_SLOT  64             /* per sheet, banked */
#define SH_STORE  0
#define SH_NAME  32

extern sheet_state_t sh;
extern handle_t      sheets;

uint16_t slot_off(uint8_t n);
void     sheet_store(void);
void     sheet_load(uint8_t n);
/* Mark the workbook changed. A macro, not a function: it sets one byte and
 * the call was most of the cost. */
extern uint8_t wb_dirty;
#define wb_touch() (wb_dirty = 1)

#endif /* X16S_WORKBOOK_PRIV_H */
