#include "xml.h"
#include <string.h>

#ifdef __CC65__
#  ifndef XML_NO_SEGMENT
#    pragma code-name (push, "OVERLAY7")
#    pragma rodata-name (push, "OVL7RO")
#    define XML_POP_SEGMENT
#  endif
#endif

#define EOS (-1)

/* --- byte input, with one byte of pushback -------------------------- */

static int16_t rd(xml_t *s)
{
    if (s->ahead >= 0) {
        int16_t c = s->ahead;
        s->ahead = -1;
        return c;
    }
    if (s->in_pos == s->in_len) {
        s->in_len = s->feed(s->ctx, s->in, XML_IN_BUF);
        s->in_pos = 0;
        if (s->in_len == 0) {
            s->eof = 1;
            return EOS;
        }
    }
    return s->in[s->in_pos++];
}

static void unrd(xml_t *s, int16_t c)
{
    s->ahead = c;
}

static uint8_t is_space(int16_t c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* A name runs to whitespace or one of the delimiters. Namespace prefixes
 * are kept whole — "r:id" stays "r:id" — because XLSX relationship
 * attributes are always written with the same prefix, and stripping it
 * would make two different attributes collide. */
static uint8_t is_name_char(int16_t c)
{
    return !(c == EOS || is_space(c) || c == '>' || c == '/'
             || c == '=' || c == '"' || c == '\'');
}

/* --- character references and UTF-8 --------------------------------- */

/* Fold a Unicode code point into the ISO-8859-15 the screen displays.
 * The Latin-1 range maps straight through; the handful of places where
 * -15 differs from -1 are the ones worth having, since a euro sign in a
 * currency column is exactly the sort of thing that turns up. */
static int16_t to_iso(uint32_t cp, xml_t *s)
{
    switch (cp) {
    case 0x20AC: return 0xA4;   /* euro          */
    case 0x0160: return 0xA6;   /* S caron       */
    case 0x0161: return 0xA8;   /* s caron       */
    case 0x017D: return 0xB4;   /* Z caron       */
    case 0x017E: return 0xB8;   /* z caron       */
    case 0x0152: return 0xBC;   /* OE ligature   */
    case 0x0153: return 0xBD;   /* oe ligature   */
    case 0x0178: return 0xBE;   /* Y diaeresis   */
    default: break;
    }
    if (cp < 0x100)
        return (int16_t)cp;
    s->lossy = 1;
    return '?';
}

/* &amp; &lt; &gt; &quot; &apos; and numeric references. An unrecognised
 * entity is passed through as its literal text rather than dropped: XLSX
 * does not produce any, and losing characters silently is worse than
 * showing them. */
static int16_t entity(xml_t *s)
{
    char buf[12];
    uint8_t n = 0;
    int16_t c;

    while (n < sizeof buf - 1) {
        c = rd(s);
        if (c == EOS)
            return EOS;
        if (c == ';')
            break;
        buf[n++] = (char)c;
    }
    buf[n] = '\0';

    {
        /* Packed, for the same reason as everywhere else in the overlays:
         * a literal stays in resident RODATA, a named const array does not.
         * The replacements are in the same order as the names. */
        static const char names[] = "amp\0lt\0gt\0quot\0apos\0";
        static const char repl[]  = "&<>\"'";
        const char *n = names;
        uint8_t i;

        for (i = 0; i < 5; ++i) {
            if (strcmp(buf, n) == 0)
                return repl[i];
            while (*n)
                ++n;
            ++n;
        }
    }

    if (buf[0] == '#') {
        uint32_t v = 0;
        uint8_t i = 1, hex = 0;
        if (buf[1] == 'x' || buf[1] == 'X') { hex = 1; i = 2; }
        for (; buf[i]; ++i) {
            char d = buf[i];
            if (d >= '0' && d <= '9')      v = v * (hex ? 16 : 10) + (d - '0');
            else if (hex && d >= 'a' && d <= 'f') v = v * 16 + (d - 'a' + 10);
            else if (hex && d >= 'A' && d <= 'F') v = v * 16 + (d - 'A' + 10);
            else return '?';
        }
        return to_iso(v, s);
    }

    return '&';                 /* unknown: keep the ampersand */
}

/* Decode one character of content: a UTF-8 sequence or an entity becomes a
 * single ISO byte. */
static int16_t content_char(xml_t *s, int16_t c)
{
    uint32_t cp;
    uint8_t extra, i;

    if (c == '&')
        return entity(s);
    if (c < 0x80)
        return c;

    /* UTF-8 continuation. Malformed input is passed through as a single
     * byte rather than resynchronised: XLSX is machine-written and this
     * keeps one bad sequence from swallowing the rest of the file. */
    if ((c & 0xE0) == 0xC0)      { cp = (uint32_t)(c & 0x1F); extra = 1; }
    else if ((c & 0xF0) == 0xE0) { cp = (uint32_t)(c & 0x0F); extra = 2; }
    else if ((c & 0xF8) == 0xF0) { cp = (uint32_t)(c & 0x07); extra = 3; }
    else                         { s->lossy = 1; return '?'; }

    for (i = 0; i < extra; ++i) {
        int16_t k = rd(s);
        if (k == EOS)
            return EOS;
        if ((k & 0xC0) != 0x80) {
            unrd(s, k);
            s->lossy = 1;
            return '?';
        }
        cp = (cp << 6) | (uint32_t)(k & 0x3F);
    }
    return to_iso(cp, s);
}

/* --- skipping the constructs XLSX contains but we ignore ------------- */

/* Run past a comment. Counting dashes rather than matching the literal
 * "-->" is what makes "<!-- x --->" end where it should: the run of dashes
 * before the '>' can be longer than two, and a naive matcher restarts on
 * the third one and never finds the terminator. */
static void skip_comment(xml_t *s)
{
    uint8_t dashes = 0;

    for (;;) {
        int16_t c = rd(s);
        if (c == EOS)
            return;
        if (c == '-') {
            if (dashes < 2)
                ++dashes;
        } else if (c == '>' && dashes >= 2) {
            return;
        } else {
            dashes = 0;
        }
    }
}

/* Run to the end of a declaration or doctype. Safe with the plain matcher
 * because neither terminator repeats its own first character. */
static void skip_until(xml_t *s, const char *close)
{
    uint8_t match = 0;

    for (;;) {
        int16_t c = rd(s);
        if (c == EOS)
            return;
        if (c == close[match]) {
            if (close[++match] == '\0')
                return;
        } else {
            /* Restart, but a mismatch that is itself the first character
             * must not be thrown away — "--->" ends a comment. */
            match = (c == close[0]) ? 1 : 0;
        }
    }
}

/* --- the tokenizer --------------------------------------------------- */

err_t xml_init(xml_t *s, xml_feed_t feed, void *ctx)
{
    memset(s, 0, sizeof *s);
    s->feed  = feed;
    s->ctx   = ctx;
    s->ahead = -1;
    s->err   = ERR_OK;
    return ERR_OK;
}

uint8_t xml_is(const xml_t *s, const char *want)
{
    return (uint8_t)(strcmp(s->name, want) == 0);
}

static void read_name(xml_t *s)
{
    uint8_t n = 0;
    int16_t c;

    for (;;) {
        c = rd(s);
        if (!is_name_char(c))
            break;
        if (n < XML_NAME_MAX - 1)
            s->name[n++] = (char)c;
    }
    s->name[n] = '\0';
    unrd(s, c);
}

/* An attribute value, entity- and UTF-8-decoded, stopping at its quote. */
static void read_attr_value(xml_t *s, int16_t quote)
{
    uint8_t n = 0;

    for (;;) {
        int16_t c = rd(s);
        if (c == EOS || c == quote)
            break;
        c = content_char(s, c);
        if (c == EOS)
            break;
        if (n < XML_VALUE_MAX - 1)
            s->value[n++] = (char)c;
    }
    s->value[n] = '\0';
    s->value_len = n;
}

xml_token_t xml_next(xml_t *s)
{
    int16_t c;

    if (s->err != ERR_OK)
        return XML_ERROR;


    if (s->in_tag) {
        /* Inside a start tag: either another attribute or the end of it. */
        do {
            c = rd(s);
        } while (is_space(c));

        if (c == EOS) {
            s->err = ERR_EOF;
            return XML_ERROR;
        }
        if (c == '/') {
            c = rd(s);                  /* expect '>' */
            s->in_tag = 0;
            /* Report the element, not whichever attribute was read last. */
            memcpy(s->name, s->elem, XML_NAME_MAX);
            return XML_END;
        }
        if (c == '>') {
            s->in_tag = 0;
            return xml_next(s);         /* on to the content */
        }

        unrd(s, c);
        read_name(s);
        do {
            c = rd(s);
        } while (is_space(c));
        if (c != '=') {
            /* A valueless attribute is not legal XML and not something
             * XLSX writes; treat it as empty rather than failing. */
            unrd(s, c);
            s->value[0] = '\0';
            s->value_len = 0;
            return XML_ATTR;
        }
        do {
            c = rd(s);
        } while (is_space(c));
        if (c != '"' && c != '\'') {
            s->err = ERR_BADFORMAT;
            return XML_ERROR;
        }
        read_attr_value(s, c);
        return XML_ATTR;
    }

    /* Content. */
    c = rd(s);
    if (c == EOS)
        return XML_EOF;

    if (c == '<') {
        c = rd(s);

        if (c == '/') {
            read_name(s);
            c = rd(s);                  /* '>' */
            return XML_END;
        }
        if (c == '?') {
            skip_until(s, "?>");
            return xml_next(s);
        }
        if (c == '!') {
            int16_t d = rd(s);
            if (d == '-') {
                rd(s);                  /* second '-' of "<!--" */
                skip_comment(s);
            } else if (d == '[') {
                /* CDATA: everything to ]]> is text, undecoded. */
                uint8_t n = 0, match = 0;
                while (n < 6) {         /* consume "CDATA[" */
                    if (rd(s) == EOS)
                        break;
                    ++n;
                }
                n = 0;
                for (;;) {
                    int16_t k = rd(s);
                    if (k == EOS)
                        break;
                    if (k == ']' && match < 2) {
                        ++match;
                        continue;
                    }
                    if (k == '>' && match == 2)
                        break;
                    /* A ']' that turned out not to end the section is
                     * real text and has to be put back. */
                    while (match) {
                        if (n < XML_VALUE_MAX - 1)
                            s->value[n++] = ']';
                        --match;
                    }
                    if (n < XML_VALUE_MAX - 1)
                        s->value[n++] = (char)k;
                    else {
                        unrd(s, k);
                        break;
                    }
                }
                s->value[n] = '\0';
                s->value_len = n;
                return XML_TEXT;
            } else {
                skip_until(s, ">");
            }
            return xml_next(s);
        }

        unrd(s, c);
        read_name(s);
        memcpy(s->elem, s->name, XML_NAME_MAX);
        s->in_tag = 1;
        return XML_START;
    }

    /* Character data, up to a buffer's worth. Longer runs come back as
     * several TEXT tokens rather than being cut off.
     *
     * The buffer check has to happen before the byte is decoded, and the
     * byte pushed back when the buffer is full. Checking afterwards drops
     * one character at every chunk boundary — invisible in short text and
     * corrupting in long. */
    {
        uint8_t n = 0;
        for (;;) {
            int16_t d;

            if (c == EOS)
                break;
            if (c == '<') {
                unrd(s, c);
                break;
            }
            if (n >= XML_VALUE_MAX - 1) {
                unrd(s, c);         /* still undecoded: safe to re-read */
                break;
            }
            d = content_char(s, c);
            if (d == EOS)
                break;
            s->value[n++] = (char)d;
            c = rd(s);
        }
        s->value[n] = '\0';
        s->value_len = n;
        return XML_TEXT;
    }
}

#ifdef XML_POP_SEGMENT
#  pragma rodata-name (pop)
#  pragma code-name (pop)
#endif
