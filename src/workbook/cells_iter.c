/* cells_iter.c — walking a sheet's rows in order.
 *
 * #included by the overlays that need it rather than compiled on its own;
 * everything here is static as a result, so three copies can coexist
 * without a rename header. See cells_priv.h for why they are not resident.
 */

/* Handle of the band containing `row`, or H_NULL. A copy of cells.c's, and
 * safely so: it reads the index and holds no state. */
static handle_t iter_band_of(const cellstore_t *cs, uint16_t row)
{
    if (cs->row_dir == H_NULL)
        return H_NULL;
    return bank_peek32(cs->row_dir, (uint16_t)(ROW_BAND(row) * 4));
}

static uint16_t cells_row_count(const cellstore_t *cs, uint16_t row)
{
    handle_t band = iter_band_of(cs, row);

    if (band == H_NULL)
        return 0;
    return bank_peek16(band, (uint16_t)(RR_OFF(row) + RR_COUNT));
}

static uint8_t cells_row_at(const cellstore_t *cs, uint16_t row, uint16_t idx,
                     cell_record_t *out)
{
    handle_t band = iter_band_of(cs, row);
    uint16_t count;
    handle_t cells;

    if (band == H_NULL)
        return 0;
    count = bank_peek16(band, (uint16_t)(RR_OFF(row) + RR_COUNT));
    if (idx >= count)
        return 0;
    cells = bank_peek32(band, (uint16_t)(RR_OFF(row) + RR_CELLS));
    bank_read(cells, (uint16_t)(idx * CELL_SIZE), out, CELL_SIZE);
    return 1;
}

