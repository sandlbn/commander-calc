/* formula_pending.c — formulas collected now, compiled in a moment.
 *
 * Two places read a file full of formulas and cannot compile them as they
 * go: the .xlsx importer, whose worksheet parser is in OVL_SHEET, and the
 * .X16S reader, which is in OVL_FILEIO. The compiler is in OVL_FCOMPILE and
 * an overlay cannot call another overlay, so both collect (sheet, row, col,
 * source-string-id) into banked RAM and leave it to resident code to load
 * the compiler once and work through the list.
 *
 * Once, for the whole list, is the point. Going through wb_set_text() per
 * formula would load OVL_FCOMPILE and then OVL_FEVAL for each one in turn:
 * two card reads apiece, four hundred times over on the inventory sheet in
 * DEMO.XLSX.
 *
 * This is resident because it is the seam between two overlays and has to
 * be somewhere neither of them is. It is small on purpose.
 *
 * THE QUEUE MUST BE ALLOCATED AFTER ANY wb_reset(). A reset re-initialises
 * the whole bank heap, so a handle taken before it addresses memory the
 * incoming workbook is then given, and the formulas written through it land
 * on the strings and cells that have just been loaded.
 */
#include "formula.h"
#include "../workbook/workbook.h"
#include "../platform/overlay.h"
#include "../platform/banked_ram.h"

/* Golden RAM: six bytes that would otherwise be six bytes of the resident
 * image. See cfg/x16sheet.cfg. */
X16S_GOLDEN_BEGIN
handle_t formula_pending;
uint16_t formula_pending_n;
X16S_GOLDEN_END

void formula_compile_pending(uint16_t *compiled)
{
    /* The count, not the handle: a handle is four bytes and cc65 charges
     * for comparing all of them, where the count is one. Nothing sets the
     * count without also setting the handle. */
    if (formula_pending_n) {
        if (ovl_require(OVL_FCOMPILE) == ERR_OK)
            formula_compile_batch(formula_pending, formula_pending_n,
                                  compiled);
    }

    /* bank_free() already ignores H_NULL, so this needs no guard of its
     * own -- and formula_compile_batch() reports through `compiled`
     * itself, so there is nothing to zero here either. */
    bank_free(formula_pending);
    formula_pending = H_NULL;
    formula_pending_n = 0;

    /* One call, not one per sheet: formula_recalc() sweeps the whole
     * workbook and restores the active sheet. A formula on a sheet nobody
     * recalculated would keep the zero it was created with. */
    wb_after_change();
}
