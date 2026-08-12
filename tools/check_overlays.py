#!/usr/bin/env python3
"""check_overlays — nothing in one overlay may call into another.

AND nothing in an overlay may call resident code that swaps overlays,
which is the same fault wearing a disguise and is checked separately
below. See swappers().

Only one overlay is resident at $8000 at a time. A call from overlay A to a
function in overlay B does not fail to link and does not fail to run: it
jumps to whatever address B's function *would* have had, which now holds a
piece of A. The machine wanders off. Renaming a sheet hung it for exactly
this reason, and nothing in the toolchain noticed.

So this reads the object files, works out which overlay each symbol is
defined in, and reports any object that references a symbol belonging to a
different one. Resident code is fine to call from anywhere; that is the
whole point of it being resident.

    make checkovl
"""
import re, subprocess, sys, glob, os

OD65 = os.path.expanduser("~/.local/bin/od65")

def dump(kind, obj):
    try:
        return subprocess.run([OD65, "--dump-" + kind, obj],
                              capture_output=True, text=True).stdout
    except FileNotFoundError:
        sys.exit("od65 not found at " + OD65)

def overlay_of(obj):
    """Which overlay this object's code lands in, or None for resident."""
    found = set()
    for m in re.finditer(r'Name:\s+"(?:OVERLAY|OVL)(\d+)', dump("segments", obj)):
        found.add(int(m.group(1)))
    return found

def names(kind, obj):
    return set(re.findall(r'Name:\s+"([^"]+)"', dump(kind, obj)))

def addresses(labels):
    """symbol -> linked address, from the VICE label file."""
    out = {}
    try:
        for line in open(labels):
            m = re.match(r'al\s+([0-9A-Fa-f]+)\s+\.?(\S+)', line.strip())
            if m:
                out[m.group(2)] = int(m.group(1), 16)
    except IOError:
        sys.exit("no " + labels + " -- run make x16 first")
    return out

def main(objdir, labels="build/labels.txt"):
    objs = sorted(glob.glob(objdir + "/**/*.o", recursive=True))
    if not objs:
        sys.exit("no objects in " + objdir + " -- run make x16 first")
    addr = addresses(labels)

    # symbol -> the overlay it is defined in.
    #
    # By address, not by which object it came from: several modules are
    # split, with most of the file resident and a few functions pushed into
    # an overlay by a pragma. number.c is mostly resident; errors.c keeps
    # the cell-error text resident and the long messages in OVL_FILEDLG.
    # Attributing every symbol in such a file to that overlay reported
    # twenty-two calls that are perfectly legal.
    #
    # Anything below the overlay area is resident and safe to call from
    # anywhere -- that is what resident means.
    OVL_BASE = 0x8000
    home = {}
    for o in objs:
        ov = overlay_of(o)
        if len(ov) != 1:
            continue
        n = next(iter(ov))
        for sym in names("exports", o):
            a = addr.get(sym)
            if a is not None and a >= OVL_BASE:
                home[sym] = n

    bad = 0
    for o in objs:
        mine = overlay_of(o)
        if not mine:
            continue                    # resident: may call anything
        for sym in names("imports", o):
            theirs = home.get(sym)
            if theirs is None:
                continue                # resident, or from the C library
            if theirs not in mine:
                print("  %s (overlay %s) calls %s (overlay %d)"
                      % (os.path.basename(o), ",".join(map(str, sorted(mine))),
                         sym, theirs))
                bad += 1

    # --- and the other way the rule gets broken -----------------------
    #
    # Resident code may call into any overlay, but only the one it has
    # loaded. Naming the wrong one in ovl_require() does not fail either:
    # the call lands on whatever is at that address. The .xlsx importer
    # asked for OVL_MENU after the sheet functions moved to OVL_FILEDLG,
    # and the machine wandered off mid-import.
    #
    # This is per file, not per function, so it is a smell test rather than
    # a proof: it catches a module calling into an overlay it never loads
    # at all, which is what that mistake looked like.
    ids = {}
    for line in open("src/x16sheet.h"):
        m = re.match(r'#define\s+(OVL_\w+)\s+(\d+)', line)
        if m:
            ids[m.group(1)] = int(m.group(2))

    for o in objs:
        if overlay_of(o):
            continue                    # not resident
        src = o.replace(objdir.rstrip("/") + "/", "", 1)[:-2] + ".c"
        if not os.path.exists(src):
            continue
        called = {home[s] for s in names("imports", o) if s in home}
        if not called:
            continue
        text = open(src).read()
        # Literal ovl_require(OVL_X) calls, plus any the file declares.
        #
        # The .xlsx driver loads its overlays through a variable -- it is a
        # state machine, and which one comes next is what each step returns
        # -- so no amount of pattern matching finds them. Such a file says
        # what it may load in an "ovl-loads:" comment, and this checks the
        # calls against that. Getting the declaration wrong is possible;
        # calling into an overlay that appears in neither is not.
        loaded = {ids[m] for m in re.findall(r'ovl_require\(\s*(OVL_\w+)',
                                             text) if m in ids}
        for decl in re.findall(r'ovl-loads:([^*\n]*)', text):
            loaded |= {ids[m] for m in re.findall(r'OVL_\w+', decl)
                       if m in ids}
        for n in sorted(called - loaded):
            print("  %s calls into overlay %d but never loads it"
                  % (os.path.basename(src), n))
            bad += 1

    bad += check_swappers()

    if bad:
        print("  %d cross-overlay call(s): these hang the machine at run time"
              % bad)
        return 1
    print("  no overlay calls into another")
    return 0


# A call these files make that looks like a swap and is not, with why.
#
# csv.c reaches wb_set_text() for every field that is NOT a formula, and
# wb_set_text only loads the compiler for one that is -- csv.c tests the
# leading '=' itself and routes those to the pending list instead. The
# guard is in the caller, so the checker cannot see it. If that test is
# ever removed, importing a CSV with a formula in it crashes again, and
# this line is the thing to come back to.
ALLOWED = {("csv.c", "wb_set_text")}


def strip_comments(text):
    """Mentioning a function in a comment is not calling it."""
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def swappers():
    """Resident functions that load an overlay, by name.

    Calling one of these from an overlay is a delayed version of calling
    into another overlay directly: the call works, the overlay it loads
    lands on top of the caller, and the RETURN goes to an address that now
    holds something else.

    Two of these got through review. sort_ovl.c ended with
    wb_after_change(), which loads OVL_FEVAL over OVL_CSV -- every sort
    returned into whatever the evaluator had put there. csv.c called
    wb_set_text(), which loads the compiler for a field beginning with '=',
    so importing a CSV with a formula in it did the same. Neither is a
    cross-overlay call by the definition above, and neither was caught.
    """
    found = {}
    for src in glob.glob("src/**/*.c", recursive=True):
        text = strip_comments(open(src, errors="ignore").read())
        if re.search(r'OVL_CODE_BEGIN\(|code-name \(push, "OVERLAY', text):
            continue                    # not resident: it IS an overlay
        # crude but sufficient: a function body reaching to the next
        # line-start '}' is the unit, and any ovl_require inside it counts
        for m in re.finditer(r'^[a-zA-Z_][\w \*]*?([a-z_][a-z_0-9]*)\s*\([^;{]*\)\s*\n?\{',
                             text, re.M):
            end = text.find("\n}", m.end())
            body = text[m.end():end if end > 0 else len(text)]
            if "ovl_require" in body:
                found[m.group(1)] = os.path.basename(src)
    return found


def check_swappers():
    sw = swappers()
    bad = 0
    for src in glob.glob("src/**/*.c", recursive=True):
        text = strip_comments(open(src, errors="ignore").read())
        if not re.search(r'OVL_CODE_BEGIN\(|code-name \(push, "OVERLAY', text):
            continue                    # resident: allowed to swap
        for fn, where in sorted(sw.items()):
            if (os.path.basename(src), fn) in ALLOWED:
                continue
            if re.search(r'\b' + fn + r'\s*\(', text):
                print("  %s calls %s(), which loads an overlay over it "
                      "(resident, %s)" % (os.path.basename(src), fn, where))
                bad += 1
    return bad

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/obj"))
