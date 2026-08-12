/* xlsx_export.c — building the parts of an .xlsx. See xlsx_export.h.
 *
 * Compiled TWICE, once into each half of the writer: xlsx_out1_x16.c takes
 * the archive and the four fixed parts, xlsx_out2_x16.c takes the number
 * formats and the cells. XW_PART selects which. On the host, where there
 * are no overlays, it is left undefined and one copy holds everything.
 *
 * The split is forced: together the writer is 10620 bytes against a
 * 7936-byte overlay area, and a quarter of that is XML text no optimisation
 * touches. Alternatives measured in docs/design/files.md.
 *
 * THE STRING TABLE BELOW IS PARTITIONED THE SAME WAY. A string only one
 * half emits must not cost space in both, and those strings are most of the
 * bulk.
 */
#include "xlsx_export.h"
#include "zipw.h"
#include "../workbook/workbook.h"
#include "../workbook/cells.h"
#include "../workbook/strings.h"
#include "../workbook/styles.h"
#include "../formula/formula.h"
#include "../util/number.h"
#include "../util/crc32_ovl.h"
#include "../util/crc32.h"
#include "../platform/banked_ram.h"
#include "../workbook/workbook_priv.h"
#include "../util/errors.h"
#include <string.h>

#include "zipw.c"          /* all static: see the note at its top */

/* sheet_name_of(): wb_sheet_name() is in OVL_FILEDLG and an overlay
 * cannot call another. Same include the .X16S writer uses. */
#define SHEET_NEW_NAME_ONLY
#include "../workbook/sheet_new.c"

#if !defined(XW_PART) || XW_PART == 2
/* The row walkers, compiled into this overlay: they are not resident.
 * See cells_priv.h. */
#include "../workbook/cells_priv.h"
#include "../workbook/cells_iter.c"
#endif

/* Each part is produced twice: once to measure it, once to write it. See
 * zipw.h for why the ZIP format leaves no cheaper option that is also
 * conventional. `measuring` is what emit() switches on. */
static uint8_t  measuring;
static uint32_t plen;
static uint32_t pcrc;
static err_t    perr;

/* The archive being written. A local would be 832 bytes of a C stack that
 * is 512 all told -- cc65 refuses it outright, which is the good outcome:
 * the same struct at file scope lives in this overlay's own bss and costs
 * the resident core nothing. */
static zipw_t  ar;

/* The parts, by kind. Anything from PK_SHEET upwards is worksheet number
 * (kind - PK_SHEET), and its name is built rather than stored -- see
 * zipw.h for why the entry table cannot afford to keep names. */
#define PK_CT     0
#define PK_RR     1
#define PK_WB     2
#define PK_WR     3
#define PK_ST     4
#define PK_SHEET  5


/* Named arrays, not literals: cc65 puts a string literal in resident
 * RODATA however rodata-name is set, and this file is largely XML
 * boilerplate. Split the way the file is -- a string only one half emits
 * must not cost space in both. */
static const char SX0[] = "&amp;";
static const char SX1[] = "&lt;";
static const char SX2[] = "&gt;";
static const char SX3[] = "&quot;";
static const char SX4[] = "0";
static const char SX5[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
static const char SX34[] = "\">";
static const char SX46[] = "1";

#if !defined(XW_PART) || XW_PART == 1
/* Split so the sheet number can go in the middle of each. */
static const char SXW_SHEETS[]     = "<sheets>";
static const char SXW_SHEET1[]     = "<sheet name=\"";
static const char SXW_SHEET2[]     = "\" sheetId=\"";
static const char SXW_SHEET3[]     = "\" r:id=\"rId";
static const char SXW_SHEET4[]     = "\"/>";
static const char SXR_ID[]  = "<Relationship Id=\"rId";
static const char SXR_WS[]  = "\" Type=\"http://schemas.openxmlformats.org/"
                              "officeDocument/2006/relationships/worksheet\""
                              " Target=\"worksheets/sheet";
static const char SXR_ST[]  = "\" Type=\"http://schemas.openxmlformats.org/"
                              "officeDocument/2006/relationships/styles\""
                              " Target=\"styles.xml\"/>";
#endif

#if !defined(XW_PART) || XW_PART == 1
static const char SX14[] = "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
static const char SX15[] = "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" Target=\"xl/workbook.xml\"/>";
static const char SX16[] = "</Relationships>";
static const char SX18[] = "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">";
static const char SX20[] = "</workbook>";
static const char SX25[] = "&quot;$&quot;#,##0";
static const char SX26[] = ".";
static const char SX27[] = "%";
static const char SX28[] = "yyyy\\-mm\\-dd";
static const char SX29[] = "hh:mm:ss";
static const char SX30[] = "@";
static const char SX31[] = "General";
static const char SX32[] = "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
static const char SX33[] = "<numFmts count=\"";
static const char SX35[] = "<numFmt numFmtId=\"";
static const char SX36[] = "\" formatCode=\"";
static const char SX37[] = "\"/>";
static const char SX38[] = "</numFmts>";
static const char SX39[] = "<fonts count=\"2\"><font><sz val=\"11\"/><name val=\"Calibri\"/></font><font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font></fonts>";
static const char SX40[] = "<fills count=\"2\"><fill><patternFill patternType=\"none\"/></fill><fill><patternFill patternType=\"gray125\"/></fill></fills>";
static const char SX41[] = "<borders count=\"1\"><border/></borders>";
static const char SX42[] = "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>";
static const char SX43[] = "<cellXfs count=\"";
static const char SX44[] = "<xf numFmtId=\"";
static const char SX45[] = "\" fontId=\"";
static const char SX47[] = "\" fillId=\"0\" borderId=\"0\" applyNumberFormat=\"1\"/>";
static const char SX48[] = "</cellXfs>";
static const char SX49[] = "</styleSheet>";
static const char SXR_END[] = ".xml\"/>";
static const char SXW_SHEETS_END[] = "</sheets>";
#endif

#if !defined(XW_PART) || XW_PART == 2
static const char SX6[] = "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">";
static const char SX7[] = "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>";
static const char SX8[] = "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
static const char SX9[] = "<Override PartName=\"/xl/workbook.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>";
static const char SX11[] = "<Override PartName=\"/xl/styles.xml\" ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>";
static const char SX12[] = "</Types>";
static const char SX51[] = "<v>";
static const char SX52[] = "</v>";
static const char SX53[] = "#VALUE!";
static const char SX54[] = "<c r=\"";
static const char SX55[] = "\"";
static const char SX56[] = " s=\"";
static const char SX57[] = " t=\"inlineStr\"><is><t>";
static const char SX58[] = "</t></is></c>";
static const char SX59[] = " t=\"b\"><v>";
static const char SX60[] = "</v></c>";
static const char SX61[] = " t=\"e\"><v>";
static const char SX62[] = "><f>";
static const char SX63[] = "</f>";
static const char SX64[] = "</c>";
static const char SX65[] = "><v>";
static const char SX66[] = "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">";
static const char SX67[] = "<cols>";
static const char SX68[] = "<col min=\"";
static const char SX69[] = "\" max=\"";
static const char SX70[] = "\" width=\"";
static const char SX71[] = "\" customWidth=\"1\"/>";
static const char SX72[] = "</cols>";
static const char SX73[] = "<sheetData>";
static const char SX74[] = "<row r=\"";
static const char SX75[] = "</row>";
static const char SX76[] = "</sheetData>";
static const char SX77[] = "</worksheet>";
static const char SXC_PART[] = "<Override PartName=\"/xl/worksheets/sheet";
static const char SXC_TYPE[] = ".xml\" ContentType=\"application/vnd."
                               "openxmlformats-officedocument.spreadsheetml."
                               "worksheet+xml\"/>";
static const char SX_T_ERR[] = " t=\"e\"";
static const char SX_T_STR[] = " t=\"str\"";
#endif

/* The part names, by kind. Anything from PK_SHEET upwards is worksheet
 * number (kind - PK_SHEET), and its name is built rather than stored --
 * see zipw.h for why the entry table cannot afford to keep names. */
static const char SX_NAME_CT[] = "[Content_Types].xml";
static const char SX_NAME_RR[] = "_rels/.rels";
static const char SX_NAME_WB[] = "xl/workbook.xml";
static const char SX_NAME_WR[] = "xl/_rels/workbook.xml.rels";
static const char SX_NAME_ST[] = "xl/styles.xml";
static const char SX_SHEET_DIR[] = "xl/worksheets/sheet";

static char part_name[ZIPW_NAME_MAX];

/* The digits are done with a comparison rather than a division: there are
 * at most sixteen sheets, and cc65 compiles / and % into runtime calls. */
static const char *part_of(uint8_t kind)
{
    uint8_t i = 0, n;

    switch (kind) {
    case PK_CT: return SX_NAME_CT;
    case PK_RR: return SX_NAME_RR;
    case PK_WB: return SX_NAME_WB;
    case PK_WR: return SX_NAME_WR;
    case PK_ST: return SX_NAME_ST;
    default:    break;
    }

    while (SX_SHEET_DIR[i]) {
        part_name[i] = SX_SHEET_DIR[i];
        ++i;
    }
    n = (uint8_t)(kind - PK_SHEET + 1);          /* 1-based in the file */
    if (n >= 10) {
        part_name[i++] = '1';
        n = (uint8_t)(n - 10);
    }
    part_name[i++] = (char)('0' + n);
    part_name[i++] = '.';
    part_name[i++] = 'x';
    part_name[i++] = 'm';
    part_name[i++] = 'l';
    part_name[i]   = '\0';
    return part_name;
}

/* --- emitting --------------------------------------------------------- */

static void emit(const char *s)
{
    uint16_t n = (uint16_t)strlen(s);

    if (perr != ERR_OK || n == 0)
        return;
    if (measuring) {
        plen += n;
        pcrc = crc32_update(pcrc, s, n);
    } else {
        zipw_data(&ar, s, n);
    }
}

/* XML text. Only the three that matter inside element content, plus quote
 * for attribute values — a spreadsheet cell full of "<" is rare and a file
 * that will not parse because of one is not. */
static void emit_esc(const char *s)
{
    char one[2];

    one[1] = '\0';
    for (; *s; ++s) {
        switch (*s) {
        case '&':  emit(SX0);  break;
        case '<':  emit(SX1);   break;
        case '>':  emit(SX2);   break;
        case '"':  emit(SX3); break;
        default:
            /* Control characters are not legal in XML 1.0 at all, and a
             * PETSCII workbook can hold them. Dropped rather than escaped:
             * an &#1; would make the file unreadable by everything. */
            if ((uint8_t)*s >= 0x20) {
                one[0] = *s;
                emit(one);
            }
            break;
        }
    }
}

static void emit_u32(uint32_t v)
{
    char tmp[11];
    uint8_t n = 0;

    if (v == 0) {
        emit(SX4);
        return;
    }
    while (v) {
        tmp[n++] = (char)('0' + (uint8_t)(v % 10));
        v /= 10;
    }
    tmp[n] = '\0';
    /* Reverse in place: the digits came out least-significant first. */
    {
        uint8_t i = 0, j = (uint8_t)(n - 1);
        while (i < j) {
            char t = tmp[i]; tmp[i] = tmp[j]; tmp[j] = t;
            ++i; --j;
        }
    }
    emit(tmp);
}

/* "A1", "IV65535" — the reference an .xlsx names a cell by. */
static void emit_ref(uint16_t row, uint16_t col)
{
    char tmp[4], name[4];
    uint8_t n = 0, i = 0;
    uint16_t c = col;

    /* Bijective base 26, the same scheme grid_col_name() uses: after the
     * first letter there is no zero digit, so each further place is
     * (c / 26 - 1). That is what makes column 26 "AA" rather than "BA". */
    for (;;) {
        tmp[n++] = (char)('A' + (c % 26));
        if (c < 26)
            break;
        c = (uint16_t)(c / 26 - 1);
    }
    while (n)
        name[i++] = tmp[--n];
    name[i] = '\0';

    emit(name);
    emit_u32((uint32_t)row + 1);
}

/* --- parts ------------------------------------------------------------ */

/* Run `build` twice and write what it produced as one entry.
 *
 * The two passes have to agree exactly, which they do because building a
 * part reads the workbook and nothing else — no clock, no allocation, no
 * state that the first pass could disturb. zipw_end() checks anyway: a
 * disagreement would otherwise be a corrupt archive nobody noticed. */
static err_t write_part(void (*build)(void), uint8_t kind)
{
    if (perr != ERR_OK)
        return perr;

    measuring = 1;
    plen = 0;
    pcrc = crc32_init();
    build();
    if (perr != ERR_OK)
        return perr;

    if (zipw_begin(&ar, part_of(kind), plen, crc32_final(pcrc)) != ERR_OK)
        return (perr = ar.err);

    measuring = 0;
    build();
    return (perr = zipw_end(&ar));
}





#if !defined(XW_PART) || XW_PART == 1

static void write_root_rels(void)
{
    emit(SX5);
    emit(SX14);
    emit(SX15);
    emit(SX16);
}

static void write_workbook(void)
{
    uint8_t i;

    emit(SX5);
    emit(SX18);
    emit(SXW_SHEETS);
    for (i = 0; i < wb_sheet_n; ++i) {
        char nm[X16S_MAX_SHEET_NAME + 1];

        sheet_name_of(i, nm);
        emit(SXW_SHEET1);           /* <sheet name=" */
        emit_esc(nm);
        emit(SXW_SHEET2);           /* " sheetId=" */
        emit_u32((uint32_t)i + 1);
        emit(SXW_SHEET3);           /* " r:id="rId */
        emit_u32((uint32_t)i + 1);
        emit(SXW_SHEET4);           /* "/> */
    }
    emit(SXW_SHEETS_END);
    emit(SX20);
}

static void write_wb_rels(void)
{
    uint8_t i;

    emit(SX5);
    emit(SX14);
    for (i = 0; i < wb_sheet_n; ++i) {
        emit(SXR_ID);               /* <Relationship Id="rId */
        emit_u32((uint32_t)i + 1);
        emit(SXR_WS);               /* " Type=".../worksheet" Target="worksheets/sheet */
        emit_u32((uint32_t)i + 1);
        emit(SXR_END);              /* .xml"/> */
    }
    /* Styles takes the next id after the sheets. */
    emit(SXR_ID);
    emit_u32((uint32_t)wb_sheet_n + 1);
    emit(SXR_ST);
    emit(SX16);
}

/* --- styles ----------------------------------------------------------- */

/* Excel's own format ids stop at 163, so anything we invent starts at 164.
 * One per style, whether or not it needs one, because that keeps the xf
 * index equal to our style id and the sheet writer needs no mapping. */
#define NUMFMT_BASE 164

static void emit_fmt_code(const cell_style_t *st)
{
    uint8_t d = st->decimal_places;

    switch (st->number_format) {
    case NF_INTEGER:  emit(SX4); break;
    case NF_DECIMAL:
    case NF_CURRENCY:
    case NF_PERCENT:
        if (st->number_format == NF_CURRENCY)
            emit(SX25);
        else
            emit(SX4);
        if (d) {
            emit(SX26);
            while (d--)
                emit(SX4);
        }
        if (st->number_format == NF_PERCENT)
            emit(SX27);
        break;
    case NF_DATE: emit(SX28); break;
    case NF_TIME: emit(SX29); break;
    case NF_TEXT: emit(SX30); break;
    default:      emit(SX31); break;
    }
}

static void write_styles(void)
{
    uint8_t n = styles_count(), i;
    cell_style_t st;

    emit(SX5);
    emit(SX32);

    emit(SX33);
    emit_u32(n);
    emit(SX34);
    for (i = 0; i < n; ++i) {
        styles_get(i, &st);
        emit(SX35);
        emit_u32((uint32_t)NUMFMT_BASE + i);
        emit(SX36);
        emit_fmt_code(&st);
        emit(SX37);
    }
    emit(SX38);

    /* The four collections below are required to be present and are read
     * before cellXfs, so a minimal one of each goes in even though this
     * program models none of them beyond bold. */
    emit(SX39);
    emit(SX40);
    emit(SX41);
    emit(SX42);

    emit(SX43);
    emit_u32(n);
    emit(SX34);
    for (i = 0; i < n; ++i) {
        styles_get(i, &st);
        emit(SX44);
        emit_u32((uint32_t)NUMFMT_BASE + i);
        emit(SX45);
        emit((st.flags & STY_BOLD) ? SX46 : SX4);
        emit(SX47);
    }
    emit(SX48);
    emit(SX49);
}

#endif /* part 1 */

#if !defined(XW_PART) || XW_PART == 2
/* --- the cells -------------------------------------------------------- */

static handle_t frec_of(const cell_record_t *rec)
{
    return (handle_t)rec->val[0]
         | ((handle_t)rec->val[1] << 8)
         | ((handle_t)rec->val[2] << 16)
         | ((handle_t)rec->val[3] << 24);
}

/* A formula's cached result, written so a reader that does not recalculate
 * still shows the right answer — which includes our own importer, since
 * translating formulas back into bytecode is a later milestone. */
static void emit_cached(const cell_record_t *rec)
{
    handle_t fr = frec_of(rec);
    uint8_t kind = bank_peek(fr, FR_KIND);
    char buf[WB_TEXT_MAX];

    if (kind == CELL_TEXT) {
        emit(SX51);
        strpool_get(bank_peek16(fr, FR_VALUE), buf, sizeof buf);
        emit_esc(buf);
        emit(SX52);
        return;
    }
    if (kind == CELL_ERROR) {
        const char *t = err_cell_text((err_t)bank_peek(fr, FR_VALUE));
        emit(SX51);
        emit(t ? t : SX53);
        emit(SX52);
        return;
    }
    /* Numbers and booleans alike, and unformatted: <v> holds the value,
     * and how it should look is the style's business. A boolean result
     * reaches here as 0 or 1, which is what <v> wants for t="b" anyway. */
    {
        snum_t v;
        bank_read(fr, FR_VALUE, v.b, 5);
        snum_to_text(&v, buf);
    }
    emit(SX51);
    emit(buf);
    emit(SX52);
}

static void emit_cell(uint16_t row, const cell_record_t *rec)
{
    char buf[WB_TEXT_MAX];

    emit(SX54);
    emit_ref(row, rec->col);
    emit(SX55);
    if (rec->style) {
        emit(SX56);
        emit_u32(rec->style);
        emit(SX55);
    }

    switch (rec->type) {
    case CELL_TEXT:
        /* Through wb_edit_text rather than the string pool directly: the
         * record's layout is the workbook's business, and this is the same
         * text the editor would show. */
        wb_edit_text(row, rec->col, buf, sizeof buf);
        emit(SX57);
        emit_esc(buf);
        emit(SX58);
        return;

    case CELL_BOOLEAN:
        emit(SX59);
        emit(rec->val[0] ? SX46 : SX4);
        emit(SX60);
        return;

    case CELL_ERROR: {
        const char *t = err_cell_text((err_t)rec->val[0]);
        emit(SX61);
        emit(t ? t : SX53);
        emit(SX60);
        return;
    }

    case CELL_FORMULA: {
        /* The cached result's type has to be declared, exactly as it does
         * for a plain cell. Without it a formula answering "Over budget" or
         * #DIV/0! is written as though the text were a number, and a reader
         * -- ours included -- drops the cell on the way back in. */
        uint8_t kind = bank_peek(frec_of(rec), FR_KIND);

        if (kind == CELL_TEXT)
            emit(SX_T_STR);
        else if (kind == CELL_ERROR)
            emit(SX_T_ERR);
        emit(SX62);
        /* The source is stored with its leading '=', which belongs to the
         * editor rather than to the file. */
        wb_edit_text(row, rec->col, buf, sizeof buf);
        emit_esc(buf[0] == '=' ? buf + 1 : buf);
        emit(SX63);
        emit_cached(rec);
        emit(SX64);
        return;
    }

    default:                            /* CELL_NUMBER, CELL_DATE */
        wb_edit_text(row, rec->col, buf, sizeof buf);
        emit(SX65);
        emit(buf);
        emit(SX60);
        return;
    }
}

static void write_sheet(void)
{
    const cellstore_t *cs = wb_cells();
    uint16_t row, i, c;

    emit(SX5);
    emit(SX66);

    /* Column widths, so a sheet that was widened here opens widened. */
    emit(SX67);
    for (c = 0; c < X16S_MAX_COLS; ++c) {
        uint8_t w = wb_col_width(c);
        if (w == X16S_DEF_COL_W)
            continue;
        emit(SX68);
        emit_u32((uint32_t)c + 1);
        emit(SX69);
        emit_u32((uint32_t)c + 1);
        emit(SX70);
        emit_u32(w);
        emit(SX71);
    }
    emit(SX72);

    emit(SX73);
    for (row = cells_next_row(cs, 0); row < X16S_MAX_ROWS;
         row = cells_next_row(cs, (uint16_t)(row + 1))) {
        uint16_t n = cells_row_count(cs, row);
        cell_record_t rec;

        emit(SX74);
        emit_u32((uint32_t)row + 1);
        emit(SX34);
        for (i = 0; i < n; ++i) {
            cells_row_at(cs, row, i, &rec);
            emit_cell(row, &rec);
        }
        emit(SX75);

        if (row == X16S_MAX_ROWS - 1)
            break;              /* next_row would wrap past the last row */
    }
    emit(SX76);
    emit(SX77);
}



static void write_content_types(void)
{
    uint8_t i;

    emit(SX5);
    emit(SX6);
    emit(SX7);
    emit(SX8);
    emit(SX9);
    for (i = 0; i < wb_sheet_n; ++i) {
        emit(SXC_PART);             /* <Override PartName="/xl/worksheets/sheet */
        emit_u32((uint32_t)i + 1);
        emit(SXC_TYPE);             /* .xml" ContentType="...worksheet+xml"/> */
    }
    emit(SX11);
    emit(SX12);
}

#endif /* part 2 */

/* --- the two halves, as the resident driver calls them ---------------- */

/* The archive is written across two overlays, so its state cannot live in
 * either of them: an overlay's data is whatever the last load left behind.
 * It goes to banked RAM between calls, four resident bytes for the handle,
 * and each half reads it in on entry and writes it back on leaving. That
 * is once per overlay swap rather than once per emit(), so the bank traffic
 * does not signify. */

static err_t state_load(void)
{
    if (xw_state == H_NULL)
        return ERR_NOMEM;
    bank_read(xw_state, 0, &ar, sizeof ar);
    return ERR_OK;
}

static void state_save(void)
{
    if (xw_state != H_NULL)
        bank_write(xw_state, 0, &ar, sizeof ar);
}

#if !defined(XW_PART) || XW_PART == 1

err_t xlsx_export_begin(const char *name)
{
    err_t e;

    perr = ERR_OK;
    xw_state = bank_alloc(sizeof ar);
    if (xw_state == H_NULL)
        return ERR_NOMEM;

    e = zipw_open(&ar, name);
    if (e != ERR_OK)
        return e;

    e = write_part(write_root_rels, PK_RR);
    if (e == ERR_OK) e = write_part(write_workbook,  PK_WB);
    if (e == ERR_OK) e = write_part(write_wb_rels,   PK_WR);
    /* styles.xml goes here rather than with the cells: it describes the
     * workbook's formats, not its contents, and the overlay that holds the
     * worksheet writer has no room for it. Order within the archive does
     * not matter -- readers follow the central directory. */
    if (e == ERR_OK) e = write_part(write_styles,    PK_ST);

    state_save();
    return e;
}

/* Always closes and always frees, whether or not it writes the directory.
 * Without one there is no archive at all -- which is the right outcome for
 * a failed export, since a file that opens with half a workbook in it would
 * be worse than one that does not open. */
err_t xlsx_export_finish(uint8_t ok)
{
    err_t e = state_load();

    if (e == ERR_OK) {
        if (ok)
            e = zipw_finish(&ar);
        else
            file_close(&ar.f);
    }
    bank_free(xw_state);
    xw_state = H_NULL;
    return e;
}

#endif /* part 1 */

#if !defined(XW_PART) || XW_PART == 2

err_t xlsx_export_body(void)
{
    err_t e = state_load();

    if (e != ERR_OK)
        return e;
    perr = ERR_OK;

    /* [Content_Types].xml is written from this half purely to balance the
     * two overlays: it is 700 bytes of fixed text and the first half had no
     * room left for it. Order within the archive does not matter, because
     * readers follow the central directory rather than scanning. */
    e = write_part(write_content_types, PK_CT);

    /* One part per worksheet. write_sheet() reads whichever sheet is
     * active, so this walks them; the user is put back where they were. */
    {
        uint8_t here = wb_sheet_i, i;

        for (i = 0; i < wb_sheet_n && e == ERR_OK; ++i) {
            wb_sheet_switch(i);
            e = write_part(write_sheet, (uint8_t)(PK_SHEET + i));
        }
        wb_sheet_switch(here);
    }

    state_save();
    return e;
}

#endif /* part 2 */
