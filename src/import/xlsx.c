#define BLOB_OWNER_XLSX
#include "blob_ovl.h"

#include "xlsx.h"
#include "xml.h"
#include "../workbook/strings.h"
#include "../platform/banked_ram.h"
#include <string.h>

#ifdef __CC65__
#  pragma code-name (push, "OVERLAY7")
#  pragma rodata-name (push, "OVL7RO")
#  pragma bss-name (push, "OVL7BSS")
#endif

/* The tokenizer is ~200 bytes, too large for cc65's stack and needed by
 * every function here in turn, never two at once. */
static xml_t X;

/* Named arrays rather than literals: cc65 places a literal in RODATA
 * however rodata-name is set, but honours the pragma for a named const
 * array. These are read only while this overlay is loaded. */
static const char S_sheets[] = "sheets";
static const char S_sheet[] = "sheet";
static const char S_name[] = "name";
static const char S_rid[] = "r:id";
static const char S_id[] = "id";
static const char S_state[] = "state";
static const char S_Relationship[] = "Relationship";
static const char S_Id[] = "Id";
static const char S_Target[] = "Target";
static const char S_xl[] = "xl/";
static const char S_si[] = "si";
static const char S_t[] = "t";



static void copy_str(char *dst, const char *src, uint8_t max)
{
    uint8_t i = 0;

    while (src[i] && i < (uint8_t)(max - 1)) {
        dst[i] = src[i];
        ++i;
    }
    dst[i] = '\0';
}

/* --- xl/workbook.xml -------------------------------------------------- */

/*   <sheets>
 *     <sheet name="Budget" sheetId="1" r:id="rId1"/>
 *     <sheet name="Old" sheetId="2" r:id="rId2" state="hidden"/>
 *   </sheets>
 *
 * Only the <sheet> elements inside <sheets> are of interest. Everything
 * else in a workbook part — defined names, calc properties, pivot caches —
 * is stepped over by simply not matching it.
 *
 * The whole part is read even after the wanted sheet is found, because
 * sheet_count is what tells the user how many were left behind.
 */
err_t xlsx_find_sheet(blob_t *xml, uint8_t n, xlsx_target_t *t)
{
    xml_token_t tok;
    uint8_t in_sheets = 0, taking = 0, seen = 0, found = 0;

    memset(t, 0, sizeof *t);
    t->index = n;
    blob_reset_read(xml);
    xml_init(&X, blob_feed, xml);

    while ((tok = xml_next(&X)) != XML_EOF && tok != XML_ERROR) {
        if (tok == XML_START) {
            if (xml_is(&X, S_sheets))
                in_sheets = 1;
            else if (in_sheets && xml_is(&X, S_sheet))
                taking = (uint8_t)(seen == n);
        } else if (tok == XML_ATTR && taking) {
            if (xml_is(&X, S_name))
                copy_str(t->name, X.value, sizeof t->name);
            else if (xml_is(&X, S_rid) || xml_is(&X, S_id))
                copy_str(t->u.rid, X.value, sizeof t->u.rid);
            else if (xml_is(&X, S_state))
                /* "hidden" and "veryHidden" both mean not shown. */
                t->hidden = (uint8_t)(X.value[0] == 'h' || X.value[0] == 'v');
        } else if (tok == XML_END) {
            if (in_sheets && xml_is(&X, S_sheet)) {
                if (taking)
                    found = 1;
                taking = 0;
                ++seen;
            } else if (xml_is(&X, S_sheets)) {
                in_sheets = 0;
            }
        }
    }

    t->sheet_count = seen;
    if (X.lossy)
        t->lossy = 1;
    if (seen == 0)
        return ERR_BADFORMAT;   /* a workbook with no sheets is not one */
    return found ? ERR_OK : ERR_NOTFOUND;
}

/* --- xl/_rels/workbook.xml.rels --------------------------------------- */

/*   <Relationship Id="rId1" Type="...worksheet" Target="worksheets/sheet1.xml"/>
 *
 * Targets are relative to the xl/ directory, so "xl/" goes in front unless
 * the target is already absolute within the package. Types other than
 * worksheet — styles, theme, sharedStrings — never match, because a sheet's
 * r:id only ever names a worksheet.
 */
err_t xlsx_find_path(blob_t *xml, xlsx_target_t *t)
{
    xml_token_t tok;
    char id[XLSX_RID_MAX];
    char target[XLSX_PATH_MAX];
    uint8_t in_rel = 0;

    if (!t->u.rid[0])
        return ERR_NOTFOUND;

    blob_reset_read(xml);
    xml_init(&X, blob_feed, xml);
    id[0] = target[0] = '\0';

    while ((tok = xml_next(&X)) != XML_EOF && tok != XML_ERROR) {
        if (tok == XML_START && xml_is(&X, S_Relationship)) {
            in_rel = 1;
            id[0] = target[0] = '\0';
        } else if (tok == XML_ATTR && in_rel) {
            if (xml_is(&X, S_Id))
                copy_str(id, X.value, sizeof id);
            else if (xml_is(&X, S_Target))
                copy_str(target, X.value, sizeof target);
        } else if (tok == XML_END && xml_is(&X, S_Relationship)) {
            in_rel = 0;
            if (!id[0] || !target[0] || strcmp(id, t->u.rid) != 0)
                continue;
            if (target[0] == '/') {
                /* An absolute target names the package root. */
                copy_str(t->u.path, target + 1, XLSX_PATH_MAX);
            } else {
                copy_str(t->u.path, S_xl, XLSX_PATH_MAX);
                copy_str(t->u.path + 3, target, (uint8_t)(XLSX_PATH_MAX - 3));
            }
            return ERR_OK;
        }
    }
    return ERR_NOTFOUND;
}

/* --- xl/sharedStrings.xml --------------------------------------------- */

/*   <si><t>Name</t></si>
 *   <si><r><t>Hello</t></r><r><t> world</t></r></si>
 *
 * The second form is rich text: several runs, each with its own formatting.
 * They are concatenated and the formatting dropped — a cell that is half
 * bold is still the same words.
 *
 * Strings go into the workbook's own pool as they are read, and `ids` gets
 * the pool id for each, indexed as the cells refer to them. That mapping is
 * what a cell's <v>42</v> resolves through, and it lives in banked RAM
 * because a workbook may declare thousands.
 */
err_t xlsx_parse_strings(blob_t *xml, handle_t ids, uint16_t max_ids,
                         uint16_t *count_out, uint8_t *lossy)
{
    xml_token_t tok;
    static char acc[X16S_MAX_TEXT_LEN];     /* overlay bss, not the stack */
    uint16_t acc_len = 0;
    uint16_t count = 0;
    uint8_t  in_si = 0, in_t = 0;

    blob_reset_read(xml);
    xml_init(&X, blob_feed, xml);

    while ((tok = xml_next(&X)) != XML_EOF && tok != XML_ERROR) {
        if (tok == XML_START) {
            if (xml_is(&X, S_si)) {
                in_si = 1;
                acc_len = 0;
            } else if (in_si && xml_is(&X, S_t)) {
                in_t = 1;
            }
        } else if (tok == XML_TEXT && in_t) {
            /* Text arrives in chunks; a long string is several tokens. */
            uint8_t k = 0;
            while (k < X.value_len && acc_len < sizeof acc - 1)
                acc[acc_len++] = X.value[k++];
        } else if (tok == XML_END) {
            if (xml_is(&X, S_t)) {
                in_t = 0;
            } else if (xml_is(&X, S_si)) {
                uint16_t id;
                acc[acc_len] = '\0';
                if (strpool_add(acc, (uint16_t)acc_len, &id) != ERR_OK)
                    return ERR_LIMIT;
                if (count < max_ids)
                    bank_poke16(ids, (uint16_t)(count * 2), id);
                ++count;
                in_si = 0;
                acc_len = 0;
            }
        }
    }

    *count_out = count;
    if (X.lossy && lossy)
        *lossy = 1;
    return count > max_ids ? ERR_LIMIT : ERR_OK;
}

#ifdef __CC65__
#  pragma bss-name (pop)
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
