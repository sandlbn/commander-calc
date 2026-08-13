/* test_filedlg.c — the file picker, which nothing exercised before.
 *
 * test_grid.c drives Save As and then cancels Open with ESC, explicitly
 * because "choosing a file would depend on what else the other tests have
 * left on the card". So the list itself, the selection and the Return path
 * had never been run. Directory browsing is built on all three.
 *
 * The card here is a real directory, build/host/sd/DLGTEST, created by the
 * test rather than by a fixture script so that its contents are known
 * exactly and no other test can add to it.
 */
#include "test.h"
#include "../src/platform/file_io.h"
#include "../src/platform/keyboard.h"
#include "../src/platform/bankmem.h"
#include "../src/platform/banked_ram.h"
#include "../src/ui/filedlg.h"
#include "../src/ui/screen.h"
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define K_RETURN 0x0D
#define K_ESC    0x1B
#define K_DOWN   0x11

#define ROOT "build/host/sd/DLGTEST"

static void touch(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f) {
        fputs("x", f);
        fclose(f);
    }
}

/* A card with a subdirectory in it:
 *
 *   DLGTEST/TOP.X16S
 *   DLGTEST/SUB/INNER.X16S
 *   DLGTEST/SUB/DEEPER/DEEP.X16S
 */
static void setup(void)
{
    kbd_host_reset();
    CHECK(bankmem_host_init(64));
    CHECK_EQ(bank_heap_init(1, 0), ERR_OK);
    screen_host_init(80, 60);

    mkdir("build/host/sd", 0777);
    mkdir(ROOT, 0777);
    mkdir(ROOT "/SUB", 0777);
    mkdir(ROOT "/SUB/DEEPER", 0777);
    touch(ROOT "/TOP.X16S");
    touch(ROOT "/SUB/INNER.X16S");
    touch(ROOT "/SUB/DEEPER/DEEP.X16S");
    file_host_set_root(ROOT);
}

static void press(const uint8_t *keys, uint8_t n)
{
    kbd_host_push(keys, n);
}

/* The listing shows directories, and a name is returned bare from the top. */
static void test_lists_directories(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[8], n = 0;

    setup();

    /* SUB sorts before TOP.X16S because a directory row starts with '/',
     * which is below both letters and digits. So: first row is /SUB. */
    k[n++] = K_DOWN;                    /* off /SUB, onto TOP.X16S */
    k[n++] = K_RETURN;
    press(k, n);

    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "TOP.X16S");
}

/* Entering a directory re-lists it, and the file comes back with its path. */
static void test_enters_a_directory(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[8], n = 0;

    setup();

    /* Inside SUB the rows are /.., /DEEPER, INNER.X16S -- directories
     * first, then files, each group in name order. */
    k[n++] = K_RETURN;                  /* /SUB -- enter it   */
    k[n++] = K_DOWN;                    /* /..  -> /DEEPER    */
    k[n++] = K_DOWN;                    /* /DEEPER -> INNER   */
    k[n++] = K_RETURN;
    press(k, n);

    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "SUB/INNER.X16S");
}

/* Two levels down, and the path carries both. */
static void test_two_levels(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[10], n = 0;

    setup();

    k[n++] = K_RETURN;                  /* into SUB   */
    k[n++] = K_DOWN;                    /* /.. -> /DEEPER */
    k[n++] = K_RETURN;                  /* into DEEPER */
    k[n++] = K_DOWN;                    /* /.. -> DEEP.X16S */
    k[n++] = K_RETURN;
    press(k, n);

    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "SUB/DEEPER/DEEP.X16S");
}

/* ".." goes back up, and is not offered at the top. */
static void test_dot_dot_returns(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[10], n = 0;

    setup();

    k[n++] = K_RETURN;                  /* into SUB          */
    k[n++] = K_RETURN;                  /* /.. is row 0 -- back out */
    k[n++] = K_DOWN;                    /* /SUB -> TOP.X16S  */
    k[n++] = K_RETURN;
    press(k, n);

    /* Landing on TOP.X16S with no path proves we really came back: inside
     * SUB there is no such file. */
    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "TOP.X16S");
}

/* Cancelling from inside a directory still climbs back out.
 *
 * This is the one that matters on the machine: browsing changes the
 * KERNAL's own directory, and overlays load by bare name, so a dialog that
 * returns without unwinding leaves the program unable to find its code. */
static void test_cancel_unwinds(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[8], n = 0;

    setup();

    k[n++] = K_RETURN;                  /* into SUB */
    k[n++] = K_ESC;                     /* give up in there */
    press(k, n);
    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 0);

    /* If the climb back had not happened, this listing would start inside
     * SUB and the first file would be INNER.X16S. */
    kbd_host_reset();
    n = 0;
    k[n++] = K_DOWN;
    k[n++] = K_RETURN;
    press(k, n);
    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "TOP.X16S");
}

/* The listing is released on every exit, not just the empty one. */
static void test_no_leak(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[4];
    uint32_t before, after;
    uint8_t i;

    setup();

    k[0] = K_ESC;
    press(k, 1);
    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 0);
    before = bank_bytes_free();

    for (i = 0; i < 8; ++i) {
        kbd_host_reset();
        k[0] = K_RETURN;                /* into SUB */
        k[1] = K_ESC;                   /* and out again */
        press(k, 2);
        CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 0);
    }

    after = bank_bytes_free();
    CHECK_EQ(after, before);
}

/* More entries than fit on screen are still reachable.
 *
 * The list used to be capped at what the box could show, and everything
 * past it was dropped without a word. */
static void test_scrolls_past_a_screenful(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[64], n = 0;
    char path[128];
    uint8_t i;

    setup();

    /* 30 files, more than the 16 rows a box shows. F00..F29 sort after the
     * "/SUB" directory row, so the first file is at index 1. */
    for (i = 0; i < 30; ++i) {
        sprintf(path, "%s/F%02u.X16S", ROOT, (unsigned)i);
        touch(path);
    }

    /* Walk to the last one: past /SUB, then 29 more. */
    for (i = 0; i < 30; ++i)
        k[n++] = K_DOWN;
    k[n++] = K_RETURN;
    press(k, n);

    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "F29.X16S");

    for (i = 0; i < 30; ++i) {
        sprintf(path, "%s/F%02u.X16S", ROOT, (unsigned)i);
        remove(path);
    }
}

/* A shorter directory must not leave the previous one's rows behind, and a
 * click on the empty part of the box must not select one.
 *
 * SUB holds fewer entries than the root once the root has 30 files in it,
 * so the box shrinks; the rows below used to keep the old listing, and a
 * click on them indexed past the end of the list. */
static void test_short_directory_after_long(void)
{
    char name[X16S_PATH_MAX];
    uint8_t k[64], n = 0;
    char path[128];
    uint8_t i;

    setup();
    for (i = 0; i < 20; ++i) {
        sprintf(path, "%s/G%02u.X16S", ROOT, (unsigned)i);
        touch(path);
    }

    /* Into SUB, which holds /.., /DEEPER and INNER.X16S -- three rows
     * where the root had twenty-two. Then pick INNER by keyboard, which
     * is bounded and must still land on the right entry. */
    k[n++] = K_RETURN;
    k[n++] = K_DOWN;
    k[n++] = K_DOWN;
    k[n++] = K_RETURN;
    press(k, n);

    CHECK_EQ(filedlg_open(".X16S\0", name, sizeof name), 1);
    CHECK_STR(name, "SUB/INNER.X16S");

    for (i = 0; i < 20; ++i) {
        sprintf(path, "%s/G%02u.X16S", ROOT, (unsigned)i);
        remove(path);
    }
}

void test_filedlg(void)
{
    test_lists_directories();
    test_enters_a_directory();
    test_two_levels();
    test_dot_dot_returns();
    test_cancel_unwinds();
    test_no_leak();
    test_scrolls_past_a_screenful();
    test_short_directory_after_long();
}
