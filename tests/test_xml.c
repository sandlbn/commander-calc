#include "test.h"
#include "../src/import/xml.h"

/* --- driving the tokenizer from a string ----------------------------- */

typedef struct {
    const char *s;
    uint16_t len, pos;
    uint16_t chunk;             /* throttle, to cross buffer refills */
} src_t;

static uint16_t src_feed(void *ctx, void *out, uint16_t want)
{
    src_t *b = (src_t *)ctx;
    uint16_t n = (uint16_t)(b->len - b->pos);

    if (n == 0)
        return 0;
    if (b->chunk && n > b->chunk)
        n = b->chunk;
    if (n > want)
        n = want;
    memcpy(out, b->s + b->pos, n);
    b->pos = (uint16_t)(b->pos + n);
    return n;
}

static xml_t X;
static src_t SRC;

static void feed(const char *text, uint16_t chunk)
{
    SRC.s = text;
    SRC.len = (uint16_t)strlen(text);
    SRC.pos = 0;
    SRC.chunk = chunk;
    xml_init(&X, src_feed, &SRC);
}

/* Render the whole token stream as one line, so a test reads like the
 * expected output rather than like twenty assertions. */
static void stream(char *out, uint16_t max)
{
    xml_token_t t;
    uint16_t n = 0;

    out[0] = '\0';
    while ((t = xml_next(&X)) != XML_EOF && t != XML_ERROR && n < max - 40) {
        const char *k = t == XML_START ? "S" :
                        t == XML_ATTR  ? "A" :
                        t == XML_TEXT  ? "T" : "E";
        if (n)
            out[n++] = ' ';
        if (t == XML_ATTR)
            n = (uint16_t)(n + (uint16_t)snprintf(out + n, max - n, "A:%s=%s",
                                                  X.name, X.value));
        else if (t == XML_TEXT)
            n = (uint16_t)(n + (uint16_t)snprintf(out + n, max - n, "T:%s",
                                                  X.value));
        else
            n = (uint16_t)(n + (uint16_t)snprintf(out + n, max - n, "%s:%s",
                                                  k, X.name));
    }
    out[n] = '\0';
}

#define STREAM(xml, want)                          \
    do {                                           \
        char b_[512];                              \
        feed((xml), 0);                            \
        stream(b_, sizeof b_);                     \
        CHECK_STR(b_, (want));                     \
    } while (0)

/* --- structure -------------------------------------------------------- */

static void test_basic(void)
{
    /* The worked example from the plan. */
    STREAM("<c r=\"A1\" t=\"s\"><v>12</v></c>",
           "S:c A:r=A1 A:t=s S:v T:12 E:v E:c");
}

static void test_self_closing(void)
{
    /* A self-closing tag must look exactly like an empty pair, so consumers
     * never have to handle both spellings. */
    STREAM("<sheet name=\"Budget\" sheetId=\"1\"/>",
           "S:sheet A:name=Budget A:sheetId=1 E:sheet");
    STREAM("<a><b/><c/></a>", "S:a S:b E:b S:c E:c E:a");
}

static void test_prolog_and_comments(void)
{
    /* Every real .xlsx part starts with a declaration; comments and
     * doctypes must not reach the consumer either. */
    STREAM("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
           "<a>x</a>", "S:a T:x E:a");
    STREAM("<!-- leading --><a><!-- inside -->y</a>", "S:a T:y E:a");
    /* A comment ending in extra dashes still ends. */
    STREAM("<a><!-- x --->z</a>", "S:a T:z E:a");
    STREAM("<!DOCTYPE whatever><a/>", "S:a E:a");
}

static void test_attributes(void)
{
    /* Single quotes are legal and LibreOffice emits them. */
    STREAM("<c r='B2' s='3'/>", "S:c A:r=B2 A:s=3 E:c");
    /* Whitespace around = and between attributes. */
    STREAM("<c  r = \"A1\"   t=\"s\" />", "S:c A:r=A1 A:t=s E:c");
    /* An empty value is a value. */
    STREAM("<c r=\"\"/>", "S:c A:r= E:c");
    /* Namespace prefixes stay attached: r:id and id are different things. */
    STREAM("<sheet r:id=\"rId1\"/>", "S:sheet A:r:id=rId1 E:sheet");
}

static void test_nesting(void)
{
    STREAM("<worksheet><sheetData><row r=\"1\">"
           "<c r=\"A1\"><v>5</v></c></row></sheetData></worksheet>",
           "S:worksheet S:sheetData S:row A:r=1 S:c A:r=A1 S:v T:5 "
           "E:v E:c E:row E:sheetData E:worksheet");
}

/* --- entities and encoding -------------------------------------------- */

static void test_entities(void)
{
    /* The five named entities, in text and in an attribute. */
    STREAM("<t>a&amp;b&lt;c&gt;d&quot;e&apos;f</t>",
           "S:t T:a&b<c>d\"e'f E:t");
    STREAM("<c v=\"&lt;&amp;&gt;\"/>", "S:c A:v=<&> E:c");

    /* Numeric references, decimal and hex. */
    STREAM("<t>&#65;&#x42;&#67;</t>", "S:t T:ABC E:t");
    /* An entity immediately followed by text. */
    STREAM("<t>&amp;x</t>", "S:t T:&x E:t");
}

static void test_utf8(void)
{
    char b[256];

    /* Two-byte sequences inside Latin-1 fold to the single ISO byte. */
    feed("<t>caf\xC3\xA9</t>", 0);
    CHECK_EQ(xml_next(&X), XML_START);
    CHECK_EQ(xml_next(&X), XML_TEXT);
    CHECK_EQ(X.value_len, 4);
    CHECK_EQ((uint8_t)X.value[3], 0xE9);         /* e-acute */
    CHECK_EQ(X.lossy, 0);

    /* The euro sign is the one character worth having from -15 over -1,
     * and it is exactly what turns up in a currency column. */
    feed("<t>\xE2\x82\xAC 5</t>", 0);
    xml_next(&X);
    CHECK_EQ(xml_next(&X), XML_TEXT);
    CHECK_EQ((uint8_t)X.value[0], 0xA4);
    CHECK_EQ(X.lossy, 0);

    /* Something with no ISO equivalent becomes '?' and is reported, so an
     * import can tell the user it changed their text. */
    feed("<t>a\xE4\xB8\xAD" "b</t>", 0);          /* CJK */
    xml_next(&X);
    CHECK_EQ(xml_next(&X), XML_TEXT);
    CHECK_STR(X.value, "a?b");
    CHECK_EQ(X.lossy, 1);

    (void)b;
}

/* --- streaming behaviour ---------------------------------------------- */

/* Text longer than the buffer must arrive as consecutive TEXT tokens, not
 * be truncated: a shared string can be far longer than anything we hold. */
static void test_long_text_is_chunked(void)
{
    static char xml[512];
    static char joined[512];
    xml_token_t t;
    uint16_t n = 0, i;
    uint8_t pieces = 0;

    strcpy(xml, "<t>");
    for (i = 0; i < 200; ++i)
        xml[3 + i] = (char)('a' + (i % 26));
    strcpy(xml + 203, "</t>");

    feed(xml, 0);
    CHECK_EQ(xml_next(&X), XML_START);
    joined[0] = '\0';
    while ((t = xml_next(&X)) == XML_TEXT) {
        ++pieces;
        memcpy(joined + n, X.value, X.value_len);
        n = (uint16_t)(n + X.value_len);
    }
    joined[n] = '\0';

    CHECK_EQ(t, XML_END);
    CHECK(pieces > 1);                  /* it really was split */
    CHECK_EQ(n, 200);
    for (i = 0; i < 200; ++i)
        if (joined[i] != (char)('a' + (i % 26))) {
            CHECK_EQ(joined[i], (char)('a' + (i % 26)));
            break;
        }
}

/* The result must not depend on how the input arrives — a tag, a name or a
 * UTF-8 sequence can straddle any refill boundary. */
static void test_input_chunking(void)
{
    static const char *doc =
        "<?xml version=\"1.0\"?><worksheet><sheetData>"
        "<row r=\"1\"><c r=\"A1\" t=\"s\"><v>15</v></c></row>"
        "</sheetData></worksheet>";
    static const char *want =
        "S:worksheet S:sheetData S:row A:r=1 S:c A:r=A1 A:t=s S:v T:15 "
        "E:v E:c E:row E:sheetData E:worksheet";
    char b[512];
    uint16_t chunks[] = { 1, 2, 3, 5, 13, 64 };
    uint8_t i;

    for (i = 0; i < sizeof chunks / sizeof chunks[0]; ++i) {
        feed(doc, chunks[i]);
        stream(b, sizeof b);
        CHECK_STR(b, want);
    }
}

/* --- robustness -------------------------------------------------------- */

static void test_cdata(void)
{
    STREAM("<t><![CDATA[a<b&c]]></t>", "S:t T:a<b&c E:t");
    /* A ']' inside the section is data, not the start of the terminator. */
    STREAM("<t><![CDATA[x]y]]></t>", "S:t T:x]y E:t");
}

static void test_unknown_constructs_are_skipped(void)
{
    /* Anything we do not model is stepped over rather than failing the
     * import: unsupported features must never stop a workbook loading. */
    STREAM("<a><?processing instruction?><b/></a>", "S:a S:b E:b E:a");
    STREAM("<worksheet><mergeCells count=\"2\"/><sheetData/></worksheet>",
           "S:worksheet S:mergeCells A:count=2 E:mergeCells "
           "S:sheetData E:sheetData E:worksheet");
}

static void test_truncated(void)
{
    xml_token_t t;
    uint8_t guard = 0;

    /* A file that stops mid-tag must end, not spin. */
    feed("<a><b r=\"x", 0);
    while ((t = xml_next(&X)) != XML_EOF && t != XML_ERROR && ++guard < 50)
        ;
    CHECK(guard < 50);
}

static void test_empty(void)
{
    feed("", 0);
    CHECK_EQ(xml_next(&X), XML_EOF);
    feed("<?xml version=\"1.0\"?>", 0);
    CHECK_EQ(xml_next(&X), XML_EOF);
}

static void test_xml_is(void)
{
    feed("<sheetData/>", 0);
    CHECK_EQ(xml_next(&X), XML_START);
    CHECK(xml_is(&X, "sheetData"));
    CHECK(!xml_is(&X, "sheet"));
    CHECK(!xml_is(&X, "sheetDataX"));
}

void test_xml(void)
{
    test_basic();
    test_self_closing();
    test_prolog_and_comments();
    test_attributes();
    test_nesting();
    test_entities();
    test_utf8();
    test_long_text_is_chunked();
    test_input_chunking();
    test_cdata();
    test_unknown_constructs_are_skipped();
    test_truncated();
    test_empty();
    test_xml_is();
}
