/* xml.h — a pull tokenizer for XML, in constant memory.
 *
 * Reads one token at a time from a callback, the same shape the decompressor
 * uses, so a worksheet of any size costs the same few hundred bytes. There
 * is no document tree and no validation: XLSX needs neither, and a DOM of a
 * megabyte worksheet is not a thing this machine can hold.
 *
 *     <c r="A1" t="s"><v>12</v></c>
 *
 * comes out as
 *
 *     START c
 *     ATTR  r = A1
 *     ATTR  t = s
 *     START v
 *     TEXT  12
 *     END   v
 *     END   c
 *
 * A self-closing <sheet name="Budget"/> yields START, its attributes, then
 * END — so a consumer never has to care which spelling was used.
 *
 * Text longer than the buffer arrives as several TEXT tokens in a row rather
 * than being truncated: a shared string can be far longer than anything we
 * would want to hold. The consumer appends.
 *
 * Encoding: XLSX is UTF-8, the display is ISO-8859-15. Sequences that have
 * an ISO equivalent are folded to it; anything else becomes '?' and sets
 * `lossy`, so an import can report that it changed the text rather than
 * silently mangling it.
 */
#ifndef X16S_XML_H
#define X16S_XML_H

#include "../util/errors.h"

#define XML_NAME_MAX  32        /* "sheetData", "mergeCells", ...        */
#define XML_VALUE_MAX 64        /* one chunk of text or an attribute     */
#define XML_IN_BUF    64

typedef uint16_t (*xml_feed_t)(void *ctx, void *buf, uint16_t len);

typedef enum {
    XML_EOF = 0,
    XML_START,          /* <name ...   — name valid                     */
    XML_ATTR,           /* name = value inside the current start tag    */
    XML_TEXT,           /* character data, value valid, maybe partial   */
    XML_END,            /* </name> or the close of <name/>              */
    XML_ERROR
} xml_token_t;

typedef struct {
    xml_feed_t feed;
    void      *ctx;

    uint8_t  in[XML_IN_BUF];
    uint16_t in_len, in_pos;
    int16_t  ahead;             /* one pushed-back byte, -1 if none     */

    char     name[XML_NAME_MAX];    /* of the current token             */
    /* The open element's name, kept separately because reading its
     * attributes overwrites `name` — and the END of a self-closing tag
     * still has to report the element, not the last attribute. */
    char     elem[XML_NAME_MAX];
    char     value[XML_VALUE_MAX];
    uint8_t  value_len;

    uint8_t  in_tag;            /* inside a start tag, reading attrs    */
    uint8_t  eof;
    uint8_t  lossy;             /* some character would not fit ISO     */

    err_t    err;
} xml_t;

err_t       xml_init(xml_t *s, xml_feed_t feed, void *ctx);
xml_token_t xml_next(xml_t *s);

/* True when `s->name` equals `want`. Saves the consumer a strcmp include
 * and reads better at the call site. */
uint8_t xml_is(const xml_t *s, const char *want);

#endif /* X16S_XML_H */
