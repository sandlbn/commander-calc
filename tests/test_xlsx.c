#include "test.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/platform/file_io.h"
#include "../src/workbook/workbook.h"
#include "../src/workbook/strings.h"
#include "../src/import/blob.h"
#include "../src/import/inflate.h"
#include "../src/import/xlsx.h"
#include "../src/import/xlsx_stage.h"
#include "../src/import/xml.h"

/* Fixtures come from tools/make_xlsx_fixtures.py, written with python's
 * zipfile so the parts are the shapes Excel and LibreOffice actually emit. */

static uint16_t nstrings;

static void setup(void)
{
    /* The 32 KB inflate window alone is four banks; a staged part needs
     * several more. */
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    CHECK_EQ(wb_init(), ERR_OK);
    file_host_set_root("build/host/sd");
}

/* Fetch one part and expand it, which is what the driver does between
 * overlay swaps. */
static err_t fetch(const char *archive, const char *path, blob_t *out)
{
    static blob_t raw;
    stage_info_t info;
    err_t e;

    /* Whatever the caller was holding goes back first: an import stages one
     * part after another and must not accumulate them. */
    blob_free(out);

    e = xlsx_stage(archive, path, &raw, &info);
    if (e != ERR_OK)
        return e;
    if (!info.compressed) {
        *out = raw;             /* stored: already the finished part */
        return ERR_OK;
    }
    e = inflate_blob(&raw, out, info.raw_size);
    blob_free(&raw);            /* the compressed copy is finished with */
    return e;
}

/* --- the blob itself -------------------------------------------------- */

static void test_blob_roundtrip(void)
{
    static blob_t b;
    uint8_t out[300];
    uint16_t i, n;
    static uint8_t in[20000];

    setup();

    /* Deliberately larger than one bank, so the bank-crossing arithmetic is
     * exercised rather than assumed. */
    for (i = 0; i < sizeof in; ++i)
        in[i] = (uint8_t)(i * 7 + (i >> 8));

    CHECK_EQ(blob_alloc(&b, sizeof in), ERR_OK);
    CHECK(b.banks >= 3);
    blob_reset_write(&b);
    /* Written in odd-sized pieces so writes straddle bank boundaries. */
    for (i = 0; i < sizeof in; i += 97)
        CHECK(blob_write(&b, in + i,
                         (uint16_t)((sizeof in - i) < 97 ? sizeof in - i : 97)));
    CHECK_EQ(b.len, sizeof in);

    blob_reset_read(&b);
    i = 0;
    while ((n = blob_read(&b, out, 300)) != 0) {
        uint16_t k;
        for (k = 0; k < n; ++k) {
            if (out[k] != in[i + k]) {
                CHECK_EQ(out[k], in[i + k]);
                bankmem_host_free();
                return;
            }
        }
        i = (uint16_t)(i + n);
    }
    CHECK_EQ(i, sizeof in);

    bankmem_host_free();
}

static void test_blob_limits(void)
{
    static blob_t b;

    setup();
    CHECK_EQ(blob_alloc(&b, 0), ERR_LIMIT);
    CHECK_EQ(blob_alloc(&b, BLOB_MAX_BYTES + 1), ERR_LIMIT);

    /* Asking for more than the machine has must refuse cleanly rather than
     * half-allocate: a partly staged part would import as a corrupt one. */
    CHECK_EQ(blob_alloc(&b, 63UL * 8192UL), ERR_LIMIT);
    bankmem_host_free();
}

/* --- staging out of the archive --------------------------------------- */

static void test_stage_stored_and_deflated(void)
{
    static blob_t b;
    stage_info_t info;

    setup();

    /* Everything in BOOK.XLSX is deflated, so this also proves the
     * compressed path end to end. */
    CHECK_EQ(xlsx_stage("BOOK.XLSX", "xl/workbook.xml", &b, &info), ERR_OK);
    CHECK_EQ(info.compressed, 1);
    CHECK(info.raw_size > 0);
    CHECK(b.len > 0);
    CHECK(b.len < info.raw_size);       /* it really is compressed */

    /* A part that is not there is not an error the caller cannot handle:
     * sharedStrings.xml is absent from workbooks with no text at all. */
    CHECK_EQ(xlsx_stage("BOOK.XLSX", "xl/nosuch.xml", &b, &info),
             ERR_NOTFOUND);
    CHECK_EQ(xlsx_stage("NOSUCH.XLSX", "xl/workbook.xml", &b, &info),
             ERR_NOTFOUND);

    bankmem_host_free();
}

static void test_inflate_blob(void)
{
    static blob_t raw, xml;
    stage_info_t info;

    setup();
    CHECK_EQ(xlsx_stage("BOOK.XLSX", "xl/workbook.xml", &raw, &info), ERR_OK);
    CHECK_EQ(inflate_blob(&raw, &xml, info.raw_size), ERR_OK);
    /* The archive's own size is the reference. */
    CHECK_EQ(xml.len, info.raw_size);

    /* A wrong expected size must be caught, not accepted: the stream and
     * the directory disagreeing is corruption.
     *
     * Re-staged first, because inflate_blob consumes its source — that is
     * what lets the blob layer live entirely inside the overlays, where
     * resident code cannot reach it. */
    CHECK_EQ(xlsx_stage("BOOK.XLSX", "xl/workbook.xml", &raw, &info), ERR_OK);
    CHECK_EQ(inflate_blob(&raw, &xml, info.raw_size + 100), ERR_BADFORMAT);

    bankmem_host_free();
}

/* --- workbook.xml ------------------------------------------------------ */

/* A banked block for the shared-string id map, which is where it lives now:
 * a workbook may declare thousands, and 8192 ids is 16 KB. */
static handle_t id_block(void)
{
    return bank_alloc(2048);
}

static void test_workbook_part(void)
{
    static blob_t xml;
    xlsx_target_t t;

    setup();
    CHECK_EQ(fetch("BOOK.XLSX", "xl/workbook.xml", &xml), ERR_OK);

    /* Sheet 0, and the count of all of them — which is what tells the user
     * how many were left behind. */
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(t.sheet_count, 3);
    CHECK_STR(t.name, "Budget");
    CHECK_STR(t.u.rid, "rId1");
    CHECK_EQ(t.hidden, 0);

    /* Order matters: it is the order the tabs appear in. */
    CHECK_EQ(xlsx_find_sheet(&xml, 1, &t), ERR_OK);
    CHECK_STR(t.name, "Inventory");
    CHECK_EQ(xlsx_find_sheet(&xml, 2, &t), ERR_OK);
    CHECK_STR(t.name, "Summary");
    CHECK_STR(t.u.rid, "rId3");
    CHECK_EQ(t.sheet_count, 3);

    /* Asking past the end is not found, and still reports the count. */
    CHECK_EQ(xlsx_find_sheet(&xml, 3, &t), ERR_NOTFOUND);
    CHECK_EQ(t.sheet_count, 3);

    bankmem_host_free();
}

static void test_hidden_sheet(void)
{
    static blob_t xml;
    xlsx_target_t t;

    setup();
    CHECK_EQ(fetch("ODD.XLSX", "xl/workbook.xml", &xml), ERR_OK);

    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_STR(t.name, "Visible");
    CHECK_EQ(t.hidden, 0);
    CHECK_EQ(xlsx_find_sheet(&xml, 1, &t), ERR_OK);
    CHECK_STR(t.name, "Gone");
    CHECK_EQ(t.hidden, 1);

    bankmem_host_free();
}

/* --- workbook.xml.rels -------------------------------------------------- */

static void test_rels(void)
{
    static blob_t xml;
    xlsx_target_t t;
    uint8_t i;

    setup();

    for (i = 0; i < 3; ++i) {
        CHECK_EQ(fetch("BOOK.XLSX", "xl/workbook.xml", &xml), ERR_OK);
        CHECK_EQ(xlsx_find_sheet(&xml, i, &t), ERR_OK);
        CHECK_EQ(fetch("BOOK.XLSX", "xl/_rels/workbook.xml.rels", &xml),
                 ERR_OK);
        CHECK_EQ(xlsx_find_path(&xml, &t), ERR_OK);

        /* Targets are relative to xl/, so the prefix has to be put back. */
        {
            char want[XLSX_PATH_MAX];
            snprintf(want, sizeof want, "xl/worksheets/sheet%u.xml", i + 1);
            CHECK_STR(t.u.path, want);
        }
        /* And the path must actually resolve in the archive — the point of
         * the whole exercise. */
        CHECK_EQ(fetch("BOOK.XLSX", t.u.path, &xml), ERR_OK);
    }

    bankmem_host_free();
}

/* A Target spelled absolutely names the package root, not the xl/ folder.
 * LibreOffice writes these. */
static void test_rels_absolute_target(void)
{
    static blob_t xml;
    xlsx_target_t t;

    setup();
    CHECK_EQ(fetch("ODD.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(fetch("ODD.XLSX", "xl/_rels/workbook.xml.rels", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_path(&xml, &t), ERR_OK);
    CHECK_STR(t.u.path, "xl/worksheets/sheet1.xml");

    bankmem_host_free();
}

/* A sheet whose relationship is missing is reported, not fatal. */
static void test_rels_missing(void)
{
    static blob_t xml;
    xlsx_target_t t;

    setup();
    CHECK_EQ(fetch("BOOK.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    strcpy(t.u.rid, "rId999");
    CHECK_EQ(fetch("BOOK.XLSX", "xl/_rels/workbook.xml.rels", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_path(&xml, &t), ERR_NOTFOUND);

    bankmem_host_free();
}

/* --- sharedStrings.xml -------------------------------------------------- */

static void test_shared_strings(void)
{
    static blob_t xml;
    xlsx_target_t t;
    handle_t ids;
    char buf[X16S_MAX_TEXT_LEN];

    setup();
    ids = id_block();
    CHECK_EQ(fetch("BOOK.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(fetch("BOOK.XLSX", "xl/sharedStrings.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_parse_strings(&xml, ids, 64, &nstrings, 0), ERR_OK);

    CHECK_EQ(nstrings, 6);

    strpool_get(bank_peek16(ids, 0), buf, sizeof buf);
    CHECK_STR(buf, "Name");
    strpool_get(bank_peek16(ids, 2), buf, sizeof buf);
    CHECK_STR(buf, "Quantity");

    /* Rich text: several runs concatenate, formatting dropped. */
    strpool_get(bank_peek16(ids, 4), buf, sizeof buf);
    CHECK_STR(buf, "Hello world");

    /* Entities decoded. */
    strpool_get(bank_peek16(ids, 6), buf, sizeof buf);
    CHECK_STR(buf, "Smith & Co");

    /* UTF-8 folded to the display charset: e-acute and the euro sign both
     * have ISO equivalents, so nothing is lost here. */
    strpool_get(bank_peek16(ids, 8), buf, sizeof buf);
    CHECK_EQ((uint8_t)buf[3], 0xE9);        /* café */
    CHECK_EQ((uint8_t)buf[5], 0xA4);        /* euro */
    CHECK_EQ(t.lossy, 0);

    /* xml:space="preserve" text keeps its padding. */
    strpool_get(bank_peek16(ids, 10), buf, sizeof buf);
    CHECK_STR(buf, " padded ");

    bankmem_host_free();
}

static void test_empty_shared_strings(void)
{
    static blob_t xml;
    xlsx_target_t t;
    handle_t ids;

    setup();
    ids = id_block();
    CHECK_EQ(fetch("ODD.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(fetch("ODD.XLSX", "xl/sharedStrings.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_parse_strings(&xml, ids, 8, &nstrings, 0), ERR_OK);
    CHECK_EQ(nstrings, 0);

    bankmem_host_free();
}

/* The whole sequence a real import runs, in order, through one archive. */
static void test_full_sequence(void)
{
    static blob_t xml;
    xlsx_target_t t;
    handle_t ids;

    setup();
    ids = id_block();

    CHECK_EQ(fetch("BOOK.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(fetch("BOOK.XLSX", "xl/_rels/workbook.xml.rels", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_path(&xml, &t), ERR_OK);
    CHECK_EQ(fetch("BOOK.XLSX", "xl/sharedStrings.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_parse_strings(&xml, ids, 64, &nstrings, 0), ERR_OK);

    CHECK_EQ(t.sheet_count, 3);
    CHECK_EQ(nstrings, 6);
    CHECK(t.name[0] != '\0');
    CHECK(t.u.path[0] != '\0');
    CHECK_EQ(fetch("BOOK.XLSX", t.u.path, &xml), ERR_OK);

    bankmem_host_free();
}

/* --- the acceptance workbook from the plan, section 35 ---------------- */

static void test_demo_workbook(void)
{
    static blob_t xml;
    xlsx_target_t t;
    handle_t ids;
    char buf[X16S_MAX_TEXT_LEN];
    uint8_t i;

    setup();
    ids = id_block();

    CHECK_EQ(fetch("DEMO.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 0, &t), ERR_OK);
    CHECK_EQ(t.sheet_count, 3);
    CHECK_STR(t.name, "Budget");

    CHECK_EQ(fetch("DEMO.XLSX", "xl/sharedStrings.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_parse_strings(&xml, ids, 128, &nstrings, 0), ERR_OK);
    CHECK_EQ(nstrings, 63);
    strpool_get(bank_peek16(ids, 0), buf, sizeof buf);
    CHECK_STR(buf, "Category");

    /* Every worksheet has to stage and expand. The inventory is the one
     * that matters: 52 KB of XML through a 128-byte buffer. */
    for (i = 0; i < 3; ++i) {
        CHECK_EQ(fetch("DEMO.XLSX", "xl/workbook.xml", &xml), ERR_OK);
        CHECK_EQ(xlsx_find_sheet(&xml, i, &t), ERR_OK);
        CHECK_EQ(fetch("DEMO.XLSX", "xl/_rels/workbook.xml.rels", &xml),
                 ERR_OK);
        CHECK_EQ(xlsx_find_path(&xml, &t), ERR_OK);
        CHECK_EQ(fetch("DEMO.XLSX", t.u.path, &xml), ERR_OK);
        if (i == 1)
            CHECK_EQ(xml.len, 52508);
    }

    /* Styles will be needed by the worksheet importer; prove it is there. */
    CHECK_EQ(fetch("DEMO.XLSX", "xl/styles.xml", &xml), ERR_OK);
    CHECK(xml.len > 1000);

    bankmem_host_free();
}

/* The large part, tokenized all the way through: if the tokenizer or the
 * decompressor go wrong anywhere in 52 KB, the counts will not come out. */
static void test_demo_large_sheet_tokenizes(void)
{
    static blob_t xml;
    xlsx_target_t t;
    static xml_t x;
    xml_token_t tok;
    uint16_t rows = 0, cells = 0, formulas = 0;

    setup();
    CHECK_EQ(fetch("DEMO.XLSX", "xl/workbook.xml", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_sheet(&xml, 1, &t), ERR_OK);
    CHECK_EQ(fetch("DEMO.XLSX", "xl/_rels/workbook.xml.rels", &xml), ERR_OK);
    CHECK_EQ(xlsx_find_path(&xml, &t), ERR_OK);
    CHECK_EQ(fetch("DEMO.XLSX", t.u.path, &xml), ERR_OK);

    blob_reset_read(&xml);
    xml_init(&x, blob_feed, &xml);
    while ((tok = xml_next(&x)) != XML_EOF && tok != XML_ERROR) {
        if (tok != XML_START)
            continue;
        if (xml_is(&x, "row"))      ++rows;
        else if (xml_is(&x, "c"))   ++cells;
        else if (xml_is(&x, "f"))   ++formulas;
    }

    CHECK_EQ(tok, XML_EOF);             /* clean end, not an error */
    CHECK_EQ(rows, 202);                /* header + 200 items + total */
    CHECK_EQ(cells, 1208);
    CHECK_EQ(formulas, 401);
    CHECK_EQ(x.lossy, 0);

    bankmem_host_free();
}

void test_xlsx(void)
{
    test_blob_roundtrip();
    test_blob_limits();
    test_stage_stored_and_deflated();
    test_inflate_blob();
    test_workbook_part();
    test_hidden_sheet();
    test_rels();
    test_rels_absolute_target();
    test_rels_missing();
    test_shared_strings();
    test_empty_shared_strings();
    test_full_sequence();
    test_demo_workbook();
    test_demo_large_sheet_tokenizes();
}
