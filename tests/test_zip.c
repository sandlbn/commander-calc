#include "test.h"
#include "../src/platform/file_io.h"
#include "../src/import/zip.h"
#include "../src/util/crc32.h"

/* The fixtures are built by tools/make_zip_fixtures.py using python's own
 * zipfile module, so this reads archives written by something other than us
 * — which is the whole point, since real input comes from Excel. */
#define ZIP "TEST.ZIP"

static void setup(void)
{
    file_host_set_root("build/host/sd");
}

static void test_open_and_count(void)
{
    zip_t z;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);
    CHECK_EQ(z.entry_count, 5);
    CHECK(z.cd_offset > 0);
    zip_close(&z);
}

/* The diagnostic the milestone asks for: list every name in the archive. */
static void test_listing(void)
{
    zip_t z;
    zip_entry_t e;
    err_t err;
    uint8_t n = 0;
    static const char *const want[] = {
        "[Content_Types].xml",
        "_rels/.rels",
        "xl/workbook.xml",
        "xl/worksheets/sheet1.xml",
        "xl/sharedStrings.xml"
    };

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);

    for (err = zip_first(&z, &e); err == ERR_OK; err = zip_next(&z, &e)) {
        if (n < 5)
            CHECK_STR(e.name, want[n]);
        ++n;
    }
    CHECK_EQ(err, ERR_EOF);         /* a clean end, not an error */
    CHECK_EQ(n, 5);

    zip_close(&z);
}

static void test_find(void)
{
    zip_t z;
    zip_entry_t e;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);

    /* Every file the XLSX importer will need to locate. */
    CHECK_EQ(zip_find(&z, "[Content_Types].xml", &e), ERR_OK);
    CHECK_EQ(e.method, ZIP_STORED);
    CHECK_EQ(e.uncomp_size, 29);

    CHECK_EQ(zip_find(&z, "xl/workbook.xml", &e), ERR_OK);
    CHECK_EQ(e.method, ZIP_DEFLATE);
    CHECK_EQ(e.uncomp_size, 105);

    CHECK_EQ(zip_find(&z, "xl/worksheets/sheet1.xml", &e), ERR_OK);
    CHECK_EQ(e.uncomp_size, 4411);
    CHECK(e.comp_size < e.uncomp_size);     /* it really is compressed */

    /* Order does not matter, and finding one does not disturb the next. */
    CHECK_EQ(zip_find(&z, "_rels/.rels", &e), ERR_OK);
    CHECK_EQ(zip_find(&z, "[Content_Types].xml", &e), ERR_OK);

    /* Missing is not found, not a corrupt archive. */
    CHECK_EQ(zip_find(&z, "xl/nosuch.xml", &e), ERR_NOTFOUND);
    /* And the format is case-sensitive, as ZIP is. */
    CHECK_EQ(zip_find(&z, "XL/WORKBOOK.XML", &e), ERR_NOTFOUND);

    zip_close(&z);
}

/* A stored entry can be read straight through, which is what makes it the
 * right thing to prove first: no decompressor involved, so a mismatch here
 * is the reader's fault and nothing else's. */
static void test_read_stored(void)
{
    zip_t z;
    zip_entry_t e;
    char buf[64];
    uint16_t n;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);
    CHECK_EQ(zip_find(&z, "[Content_Types].xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);

    n = zip_read(&z, buf, sizeof buf);
    buf[n] = '\0';
    CHECK_EQ(n, 29);
    CHECK_STR(buf, "<?xml version=\"1.0\"?><Types/>");

    /* The entry ends where it ends — no bleed into the next header. */
    CHECK_EQ(zip_read(&z, buf, sizeof buf), 0);

    zip_close(&z);
}

/* The CRC in the directory has to match what we actually read, or every
 * later stage is verifying against a number that means nothing. */
static void test_stored_crc(void)
{
    zip_t z;
    zip_entry_t e;
    uint8_t buf[16];
    uint32_t crc = crc32_init();
    uint16_t n;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);
    CHECK_EQ(zip_find(&z, "xl/sharedStrings.xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);

    /* Small reads on purpose: the streaming path is the one that matters. */
    while ((n = zip_read(&z, buf, sizeof buf)) != 0)
        crc = crc32_update(crc, buf, n);

    CHECK_EQ(crc32_final(crc), e.crc);
    zip_close(&z);
}

/* Reading a deflated entry raw must yield exactly comp_size bytes: the
 * inflate decoder in the next milestone is fed by this, and it cannot
 * recover from being given the wrong number of bytes. */
static void test_read_deflated_raw(void)
{
    zip_t z;
    zip_entry_t e;
    uint8_t buf[32];
    uint32_t total = 0;
    uint16_t n;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);
    CHECK_EQ(zip_find(&z, "xl/worksheets/sheet1.xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);

    while ((n = zip_read(&z, buf, sizeof buf)) != 0)
        total += n;

    CHECK_EQ(total, e.comp_size);
    zip_close(&z);
}

/* Entries must be readable in any order and more than once — the XLSX
 * importer reads sharedStrings, then a worksheet, then possibly styles. */
static void test_reopen_entries(void)
{
    zip_t z;
    zip_entry_t e;
    char buf[64];
    uint16_t n;

    setup();
    CHECK_EQ(zip_open(&z, ZIP), ERR_OK);

    CHECK_EQ(zip_find(&z, "xl/sharedStrings.xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);
    n = zip_read(&z, buf, sizeof buf);
    buf[n] = '\0';
    CHECK_STR(buf, "shared strings go here");

    /* Back to an earlier one... */
    CHECK_EQ(zip_find(&z, "[Content_Types].xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);
    n = zip_read(&z, buf, sizeof buf);
    CHECK_EQ(n, 29);

    /* ...and the same one again. */
    CHECK_EQ(zip_find(&z, "xl/sharedStrings.xml", &e), ERR_OK);
    CHECK_EQ(zip_open_entry(&z, &e), ERR_OK);
    n = zip_read(&z, buf, sizeof buf);
    buf[n] = '\0';
    CHECK_STR(buf, "shared strings go here");

    zip_close(&z);
}

static void test_empty_archive(void)
{
    zip_t z;
    zip_entry_t e;

    setup();
    CHECK_EQ(zip_open(&z, "EMPTY.ZIP"), ERR_OK);
    CHECK_EQ(z.entry_count, 0);
    CHECK_EQ(zip_first(&z, &e), ERR_EOF);
    CHECK_EQ(zip_find(&z, "anything", &e), ERR_NOTFOUND);
    zip_close(&z);
}

static void test_not_a_zip(void)
{
    zip_t z;
    fstream_t f;

    setup();
    CHECK_EQ(file_open_write(&f, "NOTAZIP.BIN"), ERR_OK);
    file_write(&f, "this file is not an archive at all, not even close", 49);
    file_close(&f);

    CHECK_EQ(zip_open(&z, "NOTAZIP.BIN"), ERR_BADFORMAT);
    file_remove("NOTAZIP.BIN");
}

static void test_missing_file(void)
{
    zip_t z;

    setup();
    CHECK_EQ(zip_open(&z, "NOSUCH.ZIP"), ERR_NOTFOUND);
}

/* A truncated archive must be refused rather than read into nonsense: the
 * directory offset it claims points past the end of what is there. */
static void test_truncated(void)
{
    zip_t z;
    fstream_t f;
    uint8_t data[4096];
    uint16_t n, i = 0;

    setup();
    CHECK_EQ(file_open_read(&f, ZIP), ERR_OK);
    while ((n = file_read(&f, data + i, 256)) != 0 && i < sizeof data - 256)
        i = (uint16_t)(i + n);
    file_close(&f);

    CHECK_EQ(file_open_write(&f, "CUT.ZIP"), ERR_OK);
    file_write(&f, data, (uint16_t)(i / 2));    /* half a file */
    file_close(&f);

    CHECK(zip_open(&z, "CUT.ZIP") != ERR_OK);
    file_remove("CUT.ZIP");
}

void test_zip(void)
{
    test_open_and_count();
    test_listing();
    test_find();
    test_read_stored();
    test_stored_crc();
    test_read_deflated_raw();
    test_reopen_entries();
    test_empty_archive();
    test_not_a_zip();
    test_missing_file();
    test_truncated();
}
