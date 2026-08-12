/* sheet_admin.c — adding, renaming and deleting sheets.
 *
 * In OVL_FILEDLG, with the dialogs -- not in OVL_MENU where the commands
 * are chosen. Every one of these is driven by a prompt or a confirmation,
 * and an overlay cannot call another overlay: the call jumps to an address
 * that now holds different code. Renaming a sheet hung the machine for
 * exactly that reason. Whatever asks the question and whatever does the
 * work have to be in the same overlay. See workbook_priv.h.
 */
#include "workbook_priv.h"
#include "../platform/banked_ram.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY1")
#  pragma rodata-name (push, "OVL1RO")
#  pragma bss-name (push, "OVL1BSS")
#endif

/* Not in cells.c, which is resident: deleting a sheet is the only caller
 * and resident memory is the scarcest thing in the program. */
void cells_reset(cellstore_t *cs)
{
    memset(cs, 0, sizeof *cs);
}

void wb_sheet_name(uint8_t n, char *out)
{
    out[0] = '\0';
    if (n < wb_sheet_n && sheets != H_NULL) {
        bank_read(sheets, (uint16_t)(slot_off(n) + SH_NAME), out,
                  X16S_MAX_SHEET_NAME + 1);
        out[X16S_MAX_SHEET_NAME] = '\0';
    }
}

err_t wb_sheet_rename(uint8_t n, const char *name)
{
    char buf[X16S_MAX_SHEET_NAME + 1];
    uint8_t i = 0;

    if (n >= wb_sheet_n || sheets == H_NULL)
        return ERR_NOTFOUND;
    if (name[0] == '\0')
        return ERR_VALUE;

    while (name[i] && i < X16S_MAX_SHEET_NAME) {
        buf[i] = name[i];
        ++i;
    }
    while (i <= X16S_MAX_SHEET_NAME)
        buf[i++] = '\0';

    bank_write(sheets, (uint16_t)(slot_off(n) + SH_NAME), buf, sizeof buf);
    wb_touch();
    return ERR_OK;
}

#include "sheet_new.c"          /* sheet_new(): see the note at its top */

err_t wb_sheet_add(const char *name)
{
    /* Adding does not switch: it should not move the user off the sheet
     * they are looking at. */
    return sheet_new(name);
}

err_t wb_sheet_delete(uint8_t n)
{
    sheet_state_t doomed;
    uint8_t i;

    if (n >= wb_sheet_n)
        return ERR_NOTFOUND;
    /* A workbook with no sheets has nowhere to put a cell. */
    if (wb_sheet_n == 1)
        return ERR_LIMIT;

    /* The active sheet's real state is resident, not in its slot, so it has
     * to go back before anything is shuffled over the top of it. */
    sheet_store();

    bank_read(sheets, (uint16_t)(slot_off(n) + SH_STORE), &doomed,
              sizeof doomed);
    cells_reset(&doomed.store);

    for (i = n; i + 1 < wb_sheet_n; ++i) {
        uint8_t buf[SH_SLOT];
        bank_read(sheets, slot_off((uint8_t)(i + 1)), buf, SH_SLOT);
        bank_write(sheets, slot_off(i), buf, SH_SLOT);
    }
    --wb_sheet_n;

    /* Whoever was active either moved down one or was the one deleted. */
    if (wb_sheet_i > n)
        --wb_sheet_i;
    else if (wb_sheet_i == n && wb_sheet_i >= wb_sheet_n)
        wb_sheet_i = (uint8_t)(wb_sheet_n - 1);
    sheet_load(wb_sheet_i);

    wb_touch();
    return ERR_OK;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
