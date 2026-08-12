/* editor.h — typing into a cell.
 *
 * A line buffer with a caret, nothing more. It does not know what a cell is
 * and never touches the workbook: the grid owns the decision of what to do
 * with the text on commit, which keeps the same editor usable for the
 * filename and search dialogs later.
 */
#ifndef X16S_EDITOR_H
#define X16S_EDITOR_H

#include "../util/errors.h"

#define ED_MAX 80

typedef enum {
    ED_EDITING = 0,
    ED_COMMIT,          /* Return: the caller should store the text        */
    ED_CANCEL,          /* Escape: discard                                 */
    ED_COMMIT_DOWN,     /* Return moves down, Tab moves right, and so on   */
    ED_COMMIT_RIGHT,
    ED_COMMIT_LEFT,
    ED_COMMIT_UP
} ed_result_t;

void        editor_start(const char *initial);
void        editor_stop(void);
/* Read directly. Each of these was a function that returned one byte, and
 * the call was most of the cost -- the same reason wb_sheet_n and
 * wb_sheet_i are variables. The names differ from the macros so that
 * nothing can assign to them by accident. */
extern uint8_t ed_len, ed_caret, ed_active;
#define editor_active() (ed_active)
#define editor_len()    (ed_len)
#define editor_caret()  (ed_caret)
const char *editor_text(void);

ed_result_t editor_key(uint8_t key);

#endif /* X16S_EDITOR_H */
