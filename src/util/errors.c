#include "errors.h"

/* The user-facing message table.
 *
 * One packed array of NUL-separated strings rather than a set of literals,
 * for two reasons. It is smaller — no table of pointers, and no padding.
 * And it can be placed: cc65 routes *string literals* to RODATA whatever
 * `#pragma rodata-name` says, but it does honour the pragma for a named
 * const array. That is the difference between these ~380 bytes living in
 * resident memory and living in the overlay that actually reads them.
 *
 * The dialogs are the only readers, and every call site already spells
 * dlg_message(..., err_message(e)) with that overlay loaded.
 */
#ifdef __CC65__
#  pragma rodata-name (push, "OVL1RO")
#  pragma code-name (push, "OVERLAY1")
#endif

static const char messages[] =
    "OK\0"
    "Out of memory\0"
    "Resource limit reached\0"
    "Disk error\0"
    "File not found\0"
    "Unexpected end of file\0"
    "Damaged or unrecognised file\0"
    "Checksum mismatch\0"
    "Formula syntax error\0"
    "Unknown name or function\0"
    "Invalid cell reference\0"
    "Wrong value type\0"
    "Division by zero\0"
    "Number out of range\0"
    "Circular reference\0"
    "Unsupported feature\0"
    "Import stopped at a limit\0"
    "Unknown error\0";

/* Walk to the n'th string. A linear scan of a few hundred bytes, run once
 * when a dialog is about to be drawn — cheaper in space than an index and
 * invisible in time. */
static const char *nth(uint8_t n)
{
    const char *p = messages;

    while (n--) {
        while (*p)
            ++p;
        ++p;
    }
    return p;
}

const char *err_message(err_t e)
{
    switch (e) {
    case ERR_OK:          return nth(0);
    case ERR_NOMEM:       return nth(1);
    case ERR_LIMIT:       return nth(2);
    case ERR_IO:          return nth(3);
    case ERR_NOTFOUND:    return nth(4);
    case ERR_EOF:         return nth(5);
    case ERR_BADFORMAT:   return nth(6);
    case ERR_CHECKSUM:    return nth(7);
    case ERR_SYNTAX:      return nth(8);
    case ERR_NAME:        return nth(9);
    case ERR_REF:         return nth(10);
    case ERR_VALUE:       return nth(11);
    case ERR_DIVZERO:     return nth(12);
    case ERR_NUM:         return nth(13);
    case ERR_CYCLE:       return nth(14);
    case ERR_UNSUPPORTED: return nth(15);
    case ERR_TRUNCATED:   return nth(16);
    }
    return nth(17);
}

#ifdef __CC65__
#  pragma code-name (pop)
#  pragma rodata-name (pop)
#endif

/* Resident: the cell renderer needs these on every repaint, and they are
 * six short strings. Packed the same way, for the same reason. */
/* Room for "#ERR255!" and its terminator, for a code that is none of the
 * six. Static because the answer is a `const char *` and the caller
 * strcpy()s it straight out; one cell renders at a time. */
static char odd[9];

static const char cell_errors[] =
    "#NAME?\0" "#REF!\0" "#VALUE!\0" "#DIV/0!\0" "#NUM!\0" "#CYCLE!\0";


/* The text an error cell shows. Never returns null.
 *
 * The six formula errors are consecutive in err_t, so this is a range check
 * and a subtraction rather than a six-way switch. THE ORDER OF cell_errors
 * ABOVE MUST MATCH ERR_NAME..ERR_CYCLE.
 *
 * Any other code is a fault in this program rather than in the user's
 * formula, and is rendered with its number -- "#ERR10!" is the banked
 * allocator running out. The numbers are the enum in errors.h.
 */
const char *err_cell_text(err_t e)
{
    const char *p = cell_errors;
    uint8_t n, tens = 0;

    if (e < ERR_NAME || e > ERR_CYCLE) {
        uint8_t code = (uint8_t)e, hund = 0, i = 0;

        /* Counted down rather than divided: `/ 10` and `% 10` link cc65's
         * division helpers, and nothing else resident needs them. Three
         * digits, because err_t is a byte and 255 has to come out as 255
         * rather than as whatever '0' + 25 happens to be. */
        while (code >= 100) {
            code = (uint8_t)(code - 100);
            ++hund;
        }
        while (code >= 10) {
            code = (uint8_t)(code - 10);
            ++tens;
        }
        odd[i++] = '#'; odd[i++] = 'E'; odd[i++] = 'R'; odd[i++] = 'R';
        if (hund)
            odd[i++] = (char)('0' + hund);
        if (hund || tens)
            odd[i++] = (char)('0' + tens);
        odd[i++] = (char)('0' + code);
        odd[i++] = '!';
        odd[i] = '\0';
        return odd;
    }

    /* Walked here rather than through a helper: this was the helper's only
     * caller, and the call cost more than the loop. */
    n = (uint8_t)(e - ERR_NAME);
    while (n--) {
        while (*p)
            ++p;
        ++p;
    }
    return p;
}
