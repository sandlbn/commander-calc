/* filedlg.h — the modal file dialogs.
 *
 * Lives in OVL_FILEDLG. The .X16S reader and writer are in OVL_FILEIO:
 * both only run while the user is doing something modal, and they run in
 * sequence — pick a name, then read or write it — so they never have to be
 * resident at the same time.
 *
 * Each of these takes over the screen and returns with the grid still
 * covered; the caller repaints. Doing it that way keeps the dialogs from
 * needing to know anything about the grid.
 */
#ifndef X16S_FILEDLG_H
#define X16S_FILEDLG_H

#include "../platform/file_io.h"

/* Browse the card and pick a file, listing only names ending in `ext`
 * (case-insensitive, e.g. ".X16S"). 1 if one was chosen. */
uint8_t filedlg_open(const char *ext, char *name_out, uint8_t max);

/* Type a filename, seeded with whatever is in `name`. 1 if confirmed. */
uint8_t filedlg_save_as(char *name, uint8_t max);

/* Modal notice: shows the message and waits for a key. */
void dlg_message(const char *title, const char *msg);

/* Report the outcome of an import or export: two counts with their words,
 * plus a note when something did not come through whole.
 *
 * The formatting lives here rather than at the call site because it is only
 * ever used with this overlay loaded, and its words are half a dozen short
 * strings. cc65 leaves string literals in resident memory whatever
 * rodata-name says, so a caller that spelled them out would keep them there
 * for the whole run to show one dialog. Words are chosen by number instead. */
#define RW_CELLS    0
#define RW_ROWS     1
#define RW_FORMULAS 2
#define RW_SHEETS   3
#define RW_STRINGS  4

#define RN_NONE      0
#define RN_TRUNCATED 1
#define RN_CHANGED   2
#define RN_FAILED    3          /* `title` is then the error text */
/* Some formulas came across as the value Excel had cached rather than as
 * formulas — a function this program does not have. They show the right
 * answer and will not change when their inputs do. */
#define RN_STALE     4

void dlg_result(const char *title, uint16_t a, uint8_t a_word,
                uint16_t b, uint8_t b_word, uint8_t note);

/* Report on the import that just finished, success or failure, reading it
 * from xlsx_result(). Choosing which two counters to show is six arguments
 * and four conditionals, and doing it at the call site would put all of that
 * in resident memory; here it is free. */
void dlg_xlsx_result(err_t e);

/* "Save failed", "Open failed", ... and the reason. One call rather than a
 * dlg_message() and a string constant at each of five sites: the strings
 * are identical in shape and the call sites are resident, where the same
 * text costs several times what it does here. */
#define DF_SAVE   0
#define DF_OPEN   1
#define DF_IMPORT 2
#define DF_EXPORT 3
void dlg_failed(uint8_t what, err_t e);

/* Yes/no. 1 for yes. */
uint8_t dlg_confirm(const char *title, const char *msg);

/* A titled box with one text field and a line of guidance under it. `buf`
 * supplies the initial text and receives the answer; 0 means cancelled and
 * leaves it untouched. filedlg_save_as() is this with the file wording. */
uint8_t dlg_prompt(const char *title, const char *hint, char *buf,
                   uint8_t max);

/* "Open workbook / discard unsaved changes?" and its two siblings. All
 * three ask the same question and differ only in the title, and all three
 * call sites are resident, where the text costs several times what it does
 * here. */
/* The file commands.
 *
 * Every one of them has the same shape: talk to the user, do the work, tell
 * the user how it went. Only the middle part differs, and only the middle
 * part has to be resident -- it swaps overlays, and an overlay cannot. So
 * the two conversational halves live here and grid.c keeps a switch that
 * loads the right worker and calls it.
 *
 * That is worth about 650 bytes of resident memory, which is what five
 * copies of the same skeleton were costing. */
#define FC_NEW    0
#define FC_OPEN   1
#define FC_SAVE   2
#define FC_SAVEAS 3
#define FC_IMPORT 4
#define FC_EXPORT 5

/* Ask whatever the command needs asking: confirm discarding changes, and
 * choose a filename. `name` arrives holding the current one (which Save
 * uses as-is if it has one) and leaves holding the chosen one.
 *
 * Returns 0 if the user cancelled, in which case nothing should happen. */
uint8_t filedlg_ask(uint8_t cmd, char *name, uint8_t max);

/* Say how it went: an error, or for import and export a summary of what
 * came across. `e` is what the worker returned. */
void filedlg_report(uint8_t cmd, err_t e);

/* Where an import's counts are left for filedlg_report() to find. Resident,
 * because the overlay that produced them is gone by the time it runs. */
typedef struct {
    uint16_t rows;
    uint16_t cells;
    uint8_t  truncated;
    uint8_t  is_xlsx;
} import_note_t;
extern import_note_t file_note;

#define DC_OPEN 0
#define DC_NEW  1
#define DC_QUIT 2
uint8_t dlg_discard(uint8_t what);

#endif /* X16S_FILEDLG_H */
