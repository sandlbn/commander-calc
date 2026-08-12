/* sheet_new.c — creating a worksheet in the banked table.
 *
 * #included, not compiled: two overlays need it and an overlay cannot call
 * another. OVL_FILEDLG has it behind wb_sheet_add(), which the menu and the
 * .xlsx importer use; OVL_FILEIO has its own copy for loading a .X16S,
 * which has to rebuild every sheet the file holds.
 *
 * One source rather than two similar functions, because the alternative is
 * two copies that drift: the slot has to be cleared before the name goes in
 * or a new sheet inherits the column widths of a deleted one.
 */

/* A sheet's full name, read straight from its slot.
 *
 * wb_sheet_name() does the same thing but lives in OVL_FILEDLG, and the
 * .X16S reader and writer are in two other overlays. An overlay cannot
 * call another; reading the banked table is resident work and is fine from
 * anywhere. */
static void sheet_name_of(uint8_t n, char *out)
{
    out[0] = '\0';
    if (n < wb_sheet_n && sheets != H_NULL) {
        bank_read(sheets, (uint16_t)(slot_off(n) + SH_NAME), out,
                  X16S_MAX_SHEET_NAME + 1);
        out[X16S_MAX_SHEET_NAME] = '\0';
    }
}

/* And the other way. wb_sheet_rename() is in OVL_FILEDLG for the same
 * reason wb_sheet_name() is. */
static void sheet_set_name(uint8_t n, const char *name)
{
    uint8_t buf[X16S_MAX_SHEET_NAME + 1];
    uint8_t i = 0;

    if (n >= wb_sheet_n || sheets == H_NULL)
        return;
    memset(buf, 0, sizeof buf);
    while (name[i] && i < X16S_MAX_SHEET_NAME) {
        buf[i] = (uint8_t)name[i];
        ++i;
    }
    bank_write(sheets, (uint16_t)(slot_off(n) + SH_NAME), buf, sizeof buf);
}

#ifndef SHEET_NEW_NAME_ONLY
static err_t sheet_new(const char *name)
{
    uint8_t       blank[SH_SLOT];
    sheet_state_t fresh;
    uint8_t       i = 0;
    err_t         e;

    if (wb_sheet_n >= X16S_MAX_SHEETS)
        return ERR_LIMIT;
    if (sheets == H_NULL)
        return ERR_NOMEM;

    memset(&fresh, 0, sizeof fresh);
    e = cells_init(&fresh.store);
    if (e != ERR_OK)
        return e;

    memset(blank, 0, sizeof blank);
    while (name[i] && i < X16S_MAX_SHEET_NAME) {
        blank[SH_NAME + i] = (uint8_t)name[i];
        ++i;
    }
    memcpy(blank + SH_STORE, &fresh, sizeof fresh);
    bank_write(sheets, slot_off(wb_sheet_n), blank, SH_SLOT);

    ++wb_sheet_n;
    wb_touch();
    return ERR_OK;
}
#endif /* !SHEET_NEW_NAME_ONLY */
