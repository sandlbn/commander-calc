/* x16sheet.h — shared types, limits and build configuration.
 *
 * Included by every translation unit, on both targets:
 *   - the Commander X16 (cc65, __CX16__)
 *   - the host test build (gcc, X16S_HOST)
 *
 * cc65 note: int is 16 bits, long is 32 bits, and there is no 64-bit
 * integer type and no floating point. Numbers are handled by util/number.h.
 */
#ifndef X16SHEET_H
#define X16SHEET_H

#include <stdint.h>
#include <stddef.h>

/* Must come before any string literal in any translation unit. */
#include "charmap.h"

#ifdef __CX16__
#  define X16S_TARGET_X16 1
#else
#  define X16S_HOST 1
#endif

/* ------------------------------------------------------------------ */
/* Resource limits (spec section 31)                                   */
/* ------------------------------------------------------------------ */

#define X16S_MAX_SHEETS        16
#define X16S_MAX_COLS         256      /* A .. IV                      */
#define X16S_MAX_ROWS       65535u
#define X16S_MAX_CELLS      16384u
#define X16S_MAX_STRINGS     8192u
#define X16S_MAX_STYLES       128
#define X16S_MAX_FORMULA_LEN   255
#define X16S_MAX_TEXT_LEN     1024

/* Column widths, in characters. The maximum is half an 80-column screen. */
#define X16S_DEF_COL_W          10
#define X16S_MIN_COL_W           3
#define X16S_MAX_COL_W          40
#define X16S_MAX_SHEET_NAME     31     /* plus NUL                     */

/* ------------------------------------------------------------------ */
/* Banked-RAM handles                                                  */
/*                                                                     */
/* Never store a raw pointer to banked memory; store a handle.         */
/*                                                                     */
/*   bits  0..15   offset within the bank window (only 0..8191 used)   */
/*   bits 16..23   bank number (1..255; bank 0 belongs to the KERNAL)  */
/*   bits 24..27   size class + 1, or 0 for an untracked block         */
/*                                                                     */
/* A valid handle is never 0, so H_NULL is unambiguous. The size class */
/* travels with the handle, so bank_free() takes no size argument.     */
/* Layout rationale: docs/design/memory.md.                            */
/* ------------------------------------------------------------------ */

typedef uint32_t handle_t;

#define H_NULL          ((handle_t)0)
#define H_BANK(h)       ((uint8_t)((h) >> 16))
#define H_OFF(h)        ((uint16_t)(h))
#define H_CLASS(h)      ((uint8_t)(((h) >> 24) & 0x0Fu))
#define H_MAKE(b, o)    (((handle_t)(b) << 16) | (handle_t)(o))
#define H_WITH_CLASS(h, c) ((h) | ((handle_t)((c) + 1) << 24))

#define BANK_WINDOW      0xA000u       /* where a bank appears         */
#define BANK_SIZE        0x2000u       /* 8 KB                         */

/* ------------------------------------------------------------------ */
/* Cell model                                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    CELL_EMPTY = 0,
    CELL_NUMBER,
    CELL_TEXT,
    CELL_BOOLEAN,
    CELL_DATE,
    CELL_FORMULA,
    CELL_ERROR
} cell_type_t;

/* 8 bytes. The row is implied by the row record and the column by array
 * position, and the 5-byte number lives inline, so an ordinary numeric cell
 * costs exactly one allocation. */
typedef struct {
    uint8_t col;        /* 0 .. X16S_MAX_COLS-1                        */
    uint8_t type;       /* cell_type_t                                 */
    uint8_t style;      /* index into the style table                  */
    uint8_t val[5];     /* MBF number | date serial | u16 string id |
                         * handle_t to a formula record | error code   */
} cell_record_t;

/* ------------------------------------------------------------------ */
/* Overlays                                                            */
/*                                                                     */
/* Seventeen overlays share $8000-$9EFF, one resident at a time. They  */
/* never call each other and hold no state across a swap; the ids are  */
/* the OVLn.BIN file numbers. tools/check_overlays.py enforces both    */
/* rules. See docs/design/memory.md.                                   */
/* ------------------------------------------------------------------ */

#define OVL_FILEDLG  1      /* modal file dialogs + directory listing     */
#define OVL_FILEIO   2      /* file streams, CRC, .X16S reader           */
#define OVL_FCOMPILE 3      /* formula text -> bytecode                   */
#define OVL_FEVAL    4      /* bytecode -> value, and recalculation       */
#define OVL_CSV      5      /* CSV import and export                      */
#define OVL_ZIP      6      /* ZIP package reader                         */
#define OVL_XLSX     7      /* XML tokenizer + XLSX parsers               */
#define OVL_INFLATE  8      /* DEFLATE decoder                            */
#define OVL_STYLE    9      /* XLSX number formats                        */
#define OVL_SHEET   10      /* XLSX worksheet values                      */
#define OVL_X16S_SAVE 11    /* .X16S writer, with its own file layer      */
#define OVL_XLSX_OUT1 12    /* .xlsx writer: archive + the fixed parts    */
#define OVL_XLSX_OUT2 13    /* .xlsx writer: number formats + the cells   */
#define OVL_MENU     14    /* the menu bar and its dropdowns             */
#define OVL_CHART    15    /* bar, line and pie charts                   */
#define OVL_CHARTOUT 16    /* writing a chart out as a .BMP              */
#define OVL_FREF     17    /* rewriting the references inside formulas   */
#define OVL_COUNT   17

/* Place a function in overlay `n`. Wrap the definitions, not the header.
 *
 * AN OVERLAY'S BSS IS NEVER ZEROED. crt0 clears the resident BSS segment
 * and nothing else, so an OVLnBSS static holds whatever the previous
 * overlay left there. Write it before reading it, and never keep a
 * "have I been initialised?" flag in one. */
#ifdef __CC65__
#  define OVL_CODE_BEGIN(n)  _Pragma("code-name (push, \"OVERLAY" #n "\")")
#else
#  define OVL_CODE_BEGIN(n)
#endif

/* Place a resident static in golden RAM ($0400-$07FF) instead of in bss,
 * which is the scarcer of the two. Wrap the definition:
 *
 *     X16S_GOLDEN_BEGIN
 *     static uint8_t buf[256];
 *     X16S_GOLDEN_END
 *
 * Resident code only; an overlay's statics belong in its own OVLnBSS.
 *
 * Not zeroed by the startup code. golden_init() clears the area before
 * anything else runs, which is what makes a zero-initialised static here
 * safe -- do not remove that call. */
#ifdef __CC65__
#  define X16S_GOLDEN_BEGIN  _Pragma("bss-name (push, \"GOLDENBSS\")")
#  define X16S_GOLDEN_END    _Pragma("bss-name (pop)")
#else
#  define X16S_GOLDEN_BEGIN
#  define X16S_GOLDEN_END
#endif

/* Place a global in the zero page, which shortens every reference to it by
 * a byte:
 *
 *     X16S_ZP_BEGIN
 *     grid_t grid;
 *     X16S_ZP_END
 *
 * SIXTY-FOUR BYTES, and no more: $0022-$007F less the 30 cc65's runtime
 * uses. Overrunning collides with cc65's own zero-page variables rather
 * than failing at link time, so check the ZEROPAGE and ZPBSS lines of the
 * map after adding anything. Reserve it for globals read often rather than
 * for large ones -- the saving is per reference.
 *
 * Not zeroed by the startup code; golden_init() clears this too.
 *
 * ld65 warns "address size mismatch" for every module that reads one of
 * these. It is expected and unavoidable, and the Makefile counts those
 * warnings rather than printing them. See docs/design/memory.md. */
#ifdef __CC65__
#  define X16S_ZP_BEGIN  _Pragma("bss-name (push, \"ZPBSS\", \"zp\")")
#  define X16S_ZP_END    _Pragma("bss-name (pop)")
#else
#  define X16S_ZP_BEGIN
#  define X16S_ZP_END
#endif

#endif /* X16SHEET_H */
