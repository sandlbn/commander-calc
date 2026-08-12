/* formula.h — compiling and evaluating cell formulas.
 *
 * Formula text is compiled once, to bytecode, and stored with the cell. The
 * alternative — re-parsing on every recalculation — would make a sheet of a
 * hundred formulas visibly slow on an 8 MHz machine.
 *
 *   =SUM(A1:A10)+B2   ->   PUSH_RANGE A1:A10 / CALL SUM 1 / PUSH_CELL B2 /
 *                          ADD / END
 *
 * Everything here lives in OVL_FORMULA and runs only when a formula is
 * committed or when the sheet recalculates. The renderer never calls it: a
 * formula cell carries its last computed value, so drawing the grid reads a
 * cached number rather than evaluating anything. That is what makes it safe
 * for this code to be swapped out of memory most of the time.
 *
 * The record behind a formula cell, in banked RAM:
 *
 *   0     status: FS_CLEAN or FS_DIRTY
 *   1     cached kind: CELL_NUMBER, CELL_BOOLEAN, CELL_TEXT or CELL_ERROR
 *   2-6   cached value: MBF number, or a string id, or an error code
 *   7-8   source text, as a string id
 *   9-10  bytecode length
 *   11..  bytecode
 */
#ifndef X16S_FORMULA_H
#define X16S_FORMULA_H

#include "../util/errors.h"
#include "../util/number.h"

#define FR_STATUS   0
#define FR_KIND     1
#define FR_VALUE    2
#define FR_SRC      7
#define FR_CODELEN  9
#define FR_CODE     11

#define FS_CLEAN 0
#define FS_DIRTY 1

#define FORMULA_MAX_CODE 320    /* comfortably over a 255-char source */

/* --- opcodes ------------------------------------------------------- */
enum {
    OP_END = 0,
    OP_PUSH_NUM,        /* + 5 bytes MBF                                */
    OP_PUSH_STR,        /* + u16 string id                              */
    OP_PUSH_BOOL,       /* + u8                                         */
    OP_PUSH_CELL,       /* + u16 row, u8 col                            */
    OP_PUSH_RANGE,      /* + u16 row1, u8 col1, u16 row2, u8 col2       */
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_POW, OP_NEG,
    OP_EQ, OP_NE, OP_LT, OP_GT, OP_LE, OP_GE,
    OP_CALL,            /* + u8 function, u8 argument count             */

    /* =Sheet2!A1. A PREFIX, not a push: it names the sheet the next
     * OP_PUSH_CELL or OP_PUSH_RANGE reads from, and that push clears it
     * again. One opcode rather than cross-sheet twins of both pushes, and
     * the two push cases are unchanged apart from consulting one variable.
     *
     * APPENDED ON PURPOSE, and anything added later must be appended too.
     * Bytecode is stored verbatim in a .X16S, so renumbering the opcodes
     * above would make every saved formula in every existing file mean
     * something else. Being last also keeps it outside the `op <=
     * OP_PUSH_RANGE` test that reserves stack room for a push, which this
     * is not. */
    OP_SHEET            /* + u8 sheet index                             */
};

/* --- functions ----------------------------------------------------- */
enum {
    FN_SUM = 0, FN_AVERAGE, FN_MIN, FN_MAX, FN_COUNT,
    FN_IF, FN_AND, FN_OR, FN_NOT,
    FN_ABS, FN_ROUND, FN_INT, FN_MOD, FN_LEN,
    FN__COUNT
};

/* Compile `text` (including its leading '=') and store the result as the
 * formula cell at (row,col). Returns ERR_SYNTAX/ERR_NAME/ERR_REF with
 * nothing written on failure. */
err_t formula_set(uint16_t row, uint16_t col, const char *text);

/* Rewrite every formula on the ACTIVE sheet for `by` rows inserted (+1) or
 * removed (-1) at row `at`, 0-based. In OVL_FCOMPILE: the caller loads it.
 *
 * References naming another sheet are left alone, and so is a reference to
 * the row that was deleted -- see the note beside the definition, which
 * says why for both. */
uint8_t formula_shift(uint16_t at, int8_t by, uint8_t cols, uint8_t restart);

/* --- rewriting references, in OVL_FREF ---------------------------------
 *
 * All three rewrite formula SOURCE and queue the result in
 * formula_pending; none of them compiles. Each returns 1 for "the queue is
 * full, drain it and call me again", so a sheet with more formulas than
 * FPEND_MAX is done in passes:
 *
 *     uint8_t more = 1, first = 1;
 *     while (more) {
 *         if (ovl_require(OVL_FREF) != ERR_OK) break;
 *         more = formula_shift_rows(at, by, first);
 *         first = 0;
 *         formula_compile_pending(0);     ~ loads OVL_FCOMPILE
 *     }
 *
 * `restart` says a run is beginning. It is an argument and not a static,
 * because an overlay's bss is never zeroed -- see OVL_CODE_BEGIN.
 */

/* Rewrite the formulas a sort just moved, or is about to move back.
 *
 * Reads the permutation the sort left in sort_undo_idx (see menu.h), so it
 * runs AFTER sort_run() and BEFORE sort_undo() -- the latter frees it.
 * In OVL_FCOMPILE: the caller loads it. */
uint8_t formula_sorted(uint8_t undo, uint8_t restart);

/* Inserting or deleting a COLUMN. In OVL_FREF rather than beside its row
 * twin in OVL_MENU, because that overlay is full and the reference
 * shifting a column move needs is here already. */
uint8_t col_cmd(uint8_t del);

/* Write the clipboard's FORMULA cells at (row,col), the non-formula ones
 * having already been written by OVL_MENU which cannot compile. Reads the
 * block through clip_h (see menu.h). In OVL_FCOMPILE: the caller loads it.
 *
 * A clipboard entry longer than the rewrite buffer is skipped rather than
 * truncated -- the same rule, and the same buffer, as shift_one(). */
uint8_t formula_paste(uint16_t row, uint16_t col, uint8_t restart);

/* Compile a batch the .xlsx importer collected, and report how many of them
 * this program understood. `list` is a banked block of XLSX_FFIELD-byte
 * entries: row, column, source-text pool id.
 *
 * Here rather than in the driver because the driver is resident and this
 * needs a 256-byte buffer for the source text, which is half the C stack.
 * Inside the overlay that does the compiling it costs nothing.
 *
 * A formula that will not compile is left alone: formula_set() does not
 * disturb the cell it fails on, so the value Excel cached stays put. */
err_t formula_compile_batch(handle_t list, uint16_t n, uint16_t *compiled);

/* --- formulas waiting to be compiled ----------------------------------
 *
 * The list formula_compile_batch() works through, and the resident seam
 * that gets the compiler loaded in front of it. See formula_pending.c.
 *
 * An entry is six bytes: sheet, row lo, row hi, col, id lo, id hi. */
#define FPEND_FIELD 6
#define FPEND_MAX   512
extern handle_t formula_pending;
extern uint16_t formula_pending_n;

/* Compile whatever has been collected, recalculate, and release the list.
 * Safe to call with nothing pending. Must be called from RESIDENT code:
 * it loads an overlay, so an overlay calling it would be replaced
 * mid-call.
 *
 * No error: if the compiler will not load, the formulas keep the values
 * they were read with, which is the same thing that happens to a formula
 * this program cannot parse. Nothing a caller could usefully do differs. */
void formula_compile_pending(uint16_t *compiled);

/* Recompute every formula in the sheet. Cheap when nothing is dirty. */
err_t formula_recalc(void);

/* Mark every formula dirty, so the next recalc redoes all of them. Called
 * whenever an ordinary cell changes: without dependency tracking, any edit
 * could affect any formula. */
void formula_touch_all(void);

#endif /* X16S_FORMULA_H */
