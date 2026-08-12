/* xlsx_export_drv.c — the resident half of the .xlsx writer.
 *
 * Three calls in a fixed order, which is all the sequencing an export
 * needs; contrast xlsx_import.c, where the order depends on what the
 * archive turns out to contain and the driver has to be a state machine.
 *
 * The split into two overlays is not a design preference. The writer is
 * 10620 bytes -- 2702 of that the XML text itself, which no amount of
 * optimisation shrinks -- and an overlay area is 7936.
 */
#include "xlsx_export.h"
#include "../platform/overlay.h"

/* Four resident bytes. Everything else about the archive in progress is in
 * banked RAM behind this. */
X16S_GOLDEN_BEGIN
handle_t xw_state;
X16S_GOLDEN_END

err_t xlsx_export(const char *name)
{
    err_t e = ovl_require(OVL_XLSX_OUT1);

    if (e == ERR_OK) e = xlsx_export_begin(name);
    if (e == ERR_OK) e = ovl_require(OVL_XLSX_OUT2);
    if (e == ERR_OK) e = xlsx_export_body();

    /* Back to the first overlay either way: it owns the central directory
     * and the close, and a failed export still has a file to shut. The
     * first error wins, so a bad close cannot mask a bad write. */
    if (ovl_require(OVL_XLSX_OUT1) == ERR_OK) {
        err_t f = xlsx_export_finish(e == ERR_OK);
        if (e == ERR_OK)
            e = f;
    }
    return e;
}
