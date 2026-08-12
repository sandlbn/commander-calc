/* xlsx_import.c — the resident half of the .xlsx importer.
 *
 * All of it: set the import up, then load an overlay and ask it what to load
 * next until it says nothing.
 *
 * ovl-loads: OVL_ZIP OVL_INFLATE OVL_XLSX OVL_STYLE OVL_SHEET OVL_FILEDLG
 * ovl-loads: OVL_FCOMPILE
 *
 * THAT LIST IS NOT DECORATION. The loop below calls ovl_require() with a
 * variable, so nothing can determine from the source which overlays it
 * reaches; tools/check_overlays.py reads these lines and checks every
 * overlay this file calls into appears among them. Naming the wrong one is
 * neither a link error nor a run-time error -- the call simply lands on
 * whatever code is loaded at that address.
 *
 * The work each overlay does lives beside its parser, in xlsx_step_*.c;
 * xlsx_ctx.h explains why the split falls there.
 */
#include "xlsx_ctx.h"
#include "../platform/overlay.h"
#include "../platform/banked_ram.h"
#include "inflate.h"
#include "../workbook/workbook.h"
#include "../workbook/strings.h"
#include "../formula/formula.h"
#include <string.h>

/* The import's shared state, and the largest single block of resident bss in
 * the program. It has to be resident -- five overlays read and write it
 * across the phases of one import, so it cannot live in any of them -- but
 * it does not have to be in bss, and golden RAM costs the resident image
 * nothing. xlsx_import() memsets it at the start of every import, so it
 * never depends on being zero to begin with. */
X16S_GOLDEN_BEGIN
xlsx_ctx_t xctx;
X16S_GOLDEN_END

const xlsx_report_t *xlsx_result(void)
{
    return &xctx.rep;
}

/* The worksheet the driver is about to read.
 *
 * Sheet 0 already exists -- wb_reset() made it -- so it is renamed and the
 * rest are added. Both functions live in OVL_FILEDLG with the dialogs that
 * drive them, which is why this is a step of its own: the parser's overlay
 * cannot load another. Two overlay loads per sheet, against the seconds
 * inflating takes.
 *
 * THE OVERLAY NAMED HERE MUST BE THE ONE THOSE FUNCTIONS ARE IN. Loading
 * the wrong one does not fail -- it calls whatever code sits at that
 * address, and the machine wanders off. */
err_t xlsx_open_sheet(void)
{
    if (ovl_require(OVL_FILEDLG) != ERR_OK)
        return ERR_IO;
    if (xctx.sheet_no == 0)
        return wb_sheet_rename(0, xctx.target.name);
    if (wb_sheet_n >= X16S_MAX_SHEETS) {
        xctx.rep.sheets_skipped = (uint8_t)(xctx.target.sheet_count
                                            - wb_sheet_n);
        return ERR_LIMIT;
    }
    if (wb_sheet_add(xctx.target.name) != ERR_OK)
        return ERR_LIMIT;
    return wb_sheet_switch((uint8_t)(wb_sheet_n - 1));
}

void xlsx_note_formula(uint16_t row, uint16_t col, const char *src,
                       uint16_t len)
{
    uint8_t  ent[XLSX_FFIELD];
    uint16_t sid;

    if (row >= X16S_MAX_ROWS || col >= X16S_MAX_COLS)
        return;

    /* Nowhere to put it. The list is allocated by xlsx_import(), so this is
     * H_NULL whenever the worksheet parser is driven directly rather than
     * through the driver -- which the on-target harness does.
     *
     * Without this the bank_write below went to handle 0, which is bank 0
     * at offset 0: the KERNAL's own bank. The damage surfaced much later
     * as a string pool whose records held formula bytecode, and a .X16S
     * that saved cleanly and would not load. Checked before strpool_add so
     * a reference is not taken for an entry that is dropped. */
    if (formula_pending == H_NULL)
        return;

    if (formula_pending_n >= XLSX_FORMULA_MAX) {
        xctx.rep.truncated = 1;
        return;
    }
    if (strpool_add(src, len, &sid) != ERR_OK) {
        xctx.rep.truncated = 1;
        return;
    }

    ent[0] = wb_sheet_i;
    ent[1] = (uint8_t)row;
    ent[2] = (uint8_t)(row >> 8);
    ent[3] = (uint8_t)col;
    ent[4] = (uint8_t)sid;
    ent[5] = (uint8_t)(sid >> 8);
    bank_write(formula_pending, (uint16_t)(formula_pending_n * XLSX_FFIELD),
               ent, XLSX_FFIELD);
    ++formula_pending_n;
}

err_t xlsx_import(const char *archive)
{
    uint8_t ov;

    memset(&xctx, 0, sizeof xctx);
    xctx.archive = archive;

    /* Everything goes, including whatever the user had open. An import that
     * half-replaced a workbook would be worse than one that refused. */
    xctx.err = wb_reset();
    if (xctx.err != ERR_OK)
        return xctx.err;

    xctx.ids     = bank_alloc(XLSX_ID_MAX * 2);
    xctx.styles  = bank_calloc(XLSX_STYLE_BYTES);
    formula_pending = bank_alloc(XLSX_FORMULA_MAX * XLSX_FFIELD);
    if (xctx.ids == H_NULL || xctx.styles == H_NULL
        || formula_pending == H_NULL)
        return ERR_NOMEM;

    /* The archive is where every part comes from, so the first overlay is
     * always the ZIP reader. After that each step names its successor. */
    for (ov = OVL_ZIP; ov != 0; ) {
        if (ovl_require(ov) != ERR_OK)
            return ERR_IO;

        /* A switch rather than a table of function pointers: an overlay's
         * entry point is only at its own address while that overlay is the
         * resident one, so these calls cannot be collapsed into one
         * indirect call through a resident table. */
        switch (ov) {
        case OVL_ZIP:     ov = xlsx_step_zip();     break;
        case OVL_INFLATE:
            /* One call, so it stays here: a step function for it would cost
             * 84 bytes in an overlay that has none spare, to save 60 in
             * the one place they are scarcer still. */
            xctx.err = inflate_blob(&xctx.staged, &xctx.part, xctx.raw_size);
            ov = (xctx.err != ERR_OK) ? 0 : XLSX_PARSER(xctx.part_no);
            break;
        case OVL_XLSX:
            ov = xlsx_step_xml();
            /* The workbook phase has just named this sheet, so now is when
             * it can be created. Doing it earlier would mean a placeholder
             * name; later would mean the cells had nowhere to go. */
            if (ov != 0 && xctx.part_no == XP_RELS) {
                xctx.err = xlsx_open_sheet();
                if (xctx.err != ERR_OK)
                    ov = 0;
            }
            break;
        case OVL_STYLE:   ov = xlsx_step_style();   break;
        default:
            ov = xlsx_step_sheet();
            /* Round again for the next worksheet, if the workbook has one
             * and there is room for it. Everything per-sheet resets; the
             * shared-string map and the styles are the workbook's and stay
             * exactly as they are. */
            if (ov == 0 && xctx.err == ERR_OK
                && xctx.sheet_no + 1 < xctx.target.sheet_count) {
                if (wb_sheet_n >= X16S_MAX_SHEETS) {
                    xctx.rep.sheets_skipped =
                        (uint8_t)(xctx.target.sheet_count - wb_sheet_n);
                } else {
                    ++xctx.sheet_no;
                    xctx.part_no = XP_WORKBOOK;
                    ov = OVL_ZIP;
                }
            }
            break;
        }
    }

    /* --- the formulas ---------------------------------------------------
     *
     * Collected by the worksheet parser and compiled here, with the
     * compiler loaded once for all of them. Going through wb_set_text()
     * instead would load OVL_FCOMPILE and then OVL_FEVAL for each formula
     * in turn: two SD reads apiece, four hundred times over on the
     * inventory sheet in DEMO.XLSX.
     *
     * Failures are not failures of the import. A formula this program does
     * not understand keeps the value Excel cached, which is the right
     * answer until one of its inputs changes. */
    if (xctx.err == ERR_OK)
        formula_compile_pending(&xctx.rep.formulas_live);

    /* No recalculation loop here: formula_compile_pending() above sweeps
     * the whole workbook, which it must, since a formula can refer across
     * sheets. */

    /* Leave the user looking at the first sheet rather than the last one
     * the importer happened to fill. */
    wb_sheet_switch(0);

    /* An imported workbook has not been saved anywhere, so it counts as
     * modified: closing it without saving would lose the import. */
    return xctx.err;
}
