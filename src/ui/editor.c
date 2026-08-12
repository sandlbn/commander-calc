#include "editor.h"
#include "../x16sheet.h"
#include <string.h>

#define K_RETURN    0x0D
#define K_DEL       0x14        /* backspace */
#define K_FWDDEL    0x19
#define K_ESC       0x1B
#define K_TAB       0x09
#define K_SHIFTTAB  0x18
#define K_LEFT      0x9D
#define K_RIGHT     0x1D
#define K_UP        0x91
#define K_DOWN      0x11
#define K_HOME      0x13
#define K_END       0x04

/* The line being typed: 81 bytes, the largest single resident buffer, and
 * written from the first keystroke before anything reads it. */
X16S_GOLDEN_BEGIN
static char    buf[ED_MAX + 1];

/* Not static, and read directly rather than through accessors (see
 * editor.h): three functions each returning one byte cost more than the
 * bytes.
 *
 * Zero page -- the editor touches all three on every keystroke. */
X16S_ZP_BEGIN
uint8_t ed_len, ed_caret, ed_active;
X16S_ZP_END
X16S_GOLDEN_END

void editor_start(const char *initial)
{
    ed_len = 0;
    if (initial) {
        while (initial[ed_len] && ed_len < ED_MAX) {
            buf[ed_len] = initial[ed_len];
            ++ed_len;
        }
    }
    buf[ed_len] = '\0';
    ed_caret = ed_len;
    ed_active = 1;
}

void editor_stop(void)
{
    ed_active = 0;
    ed_len = ed_caret = 0;
    buf[0] = '\0';
}

const char *editor_text(void)   { return buf; }

static void insert(char c)
{
    uint8_t i;

    if (ed_len >= ED_MAX)
        return;
    for (i = ed_len; i > ed_caret; --i)
        buf[i] = buf[i - 1];
    buf[ed_caret++] = c;
    ++ed_len;
    buf[ed_len] = '\0';
}

static void backspace(void)
{
    uint8_t i;

    if (ed_caret == 0)
        return;
    for (i = (uint8_t)(ed_caret - 1); i < ed_len - 1; ++i)
        buf[i] = buf[i + 1];
    --ed_caret;
    --ed_len;
    buf[ed_len] = '\0';
}

static void fwd_delete(void)
{
    uint8_t i;

    if (ed_caret >= ed_len)
        return;
    for (i = ed_caret; i < ed_len - 1; ++i)
        buf[i] = buf[i + 1];
    --ed_len;
    buf[ed_len] = '\0';
}

ed_result_t editor_key(uint8_t key)
{
    if (!ed_active)
        return ED_EDITING;

    switch (key) {
    case K_RETURN:   return ED_COMMIT_DOWN;
    case K_TAB:      return ED_COMMIT_RIGHT;
    case K_SHIFTTAB: return ED_COMMIT_LEFT;
    /* Up and down commit and move, the way every spreadsheet behaves —
     * they do not navigate within the text. */
    case K_UP:       return ED_COMMIT_UP;
    case K_DOWN:     return ED_COMMIT_DOWN;
    case K_ESC:      return ED_CANCEL;

    case K_DEL:      backspace(); return ED_EDITING;
    case K_FWDDEL:   fwd_delete(); return ED_EDITING;

    case K_LEFT:     if (ed_caret) --ed_caret; return ED_EDITING;
    case K_RIGHT:    if (ed_caret < ed_len) ++ed_caret; return ED_EDITING;
    case K_HOME:     ed_caret = 0; return ED_EDITING;
    case K_END:      ed_caret = ed_len; return ED_EDITING;

    default:
        if (key >= 0x20 && key < 0x7F)
            insert((char)key);
        return ED_EDITING;
    }
}
