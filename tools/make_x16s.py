#!/usr/bin/env python3
"""make_x16s — write .X16S workbooks directly, without the machine.

    python3 tools/make_x16s.py <dir>

The native format, which is what the program saves and what it opens
fastest: no ZIP, no inflating, no XML, just chunks read straight into the
cell store. An .xlsx of 500 days of prices takes 51 seconds to open; the
same workbook as .X16S is a fraction of that, because none of the three
expensive phases happen at all.

Writing them here rather than by importing and saving on the machine means
the loader can be tested against files it did not produce -- which is the
only way to find out whether it reads the FORMAT or merely reads its own
output. docs/x16s-format.md is the description this is written from; if the
two ever disagree, the reader in src/workbook/native_file.c is the truth.
"""
import sys, os, struct, zlib, datetime

VERSION = 3
EPOCH   = datetime.date(1899, 12, 30)

# cell_type_t, from src/x16sheet.h
T_EMPTY, T_NUMBER, T_TEXT, T_BOOLEAN, T_DATE, T_FORMULA, T_ERROR = range(7)

# number_format_t, from src/workbook/styles.h. Copied by hand once and got
# wrong once: NF_INTEGER and NF_DECIMAL come before the interesting ones, so
# an enum guessed from the format names is off by one from NF_CURRENCY on,
# and the symptom is a date column rendered as percentages.
(NF_GENERAL, NF_INTEGER, NF_DECIMAL, NF_CURRENCY,
 NF_PERCENT, NF_DATE, NF_TIME, NF_TEXT) = range(8)


def mbf(x):
    """A number as the 5-byte Microsoft Binary Format the X16 ROM uses.

    Byte 0 is the exponent in excess-128, zero meaning the value is zero.
    Bytes 1-4 are the mantissa, most significant first, normalised so the
    top bit is always 1 -- which is why that bit carries the sign instead.
    """
    if x == 0:
        return b"\0\0\0\0\0"
    sign = 0x80 if x < 0 else 0
    x = abs(float(x))
    exp = 0
    while x >= 1.0:
        x /= 2.0
        exp += 1
    while x < 0.5:
        x *= 2.0
        exp -= 1
    m = int(round(x * (1 << 32)))
    if m >= (1 << 32):          # rounding can carry out of the mantissa
        m >>= 1
        exp += 1
    b = m.to_bytes(4, "big")
    return bytes([exp + 128, (b[0] & 0x7F) | sign, b[1], b[2], b[3]])


class Sheet:
    def __init__(self, name, widths=None):
        self.name = name
        self.widths = dict(enumerate(widths or []))
        self.cells = {}                 # (row, col) -> (type, style, val5)

    def num(self, row, col, v, style=0):
        self.cells[(row, col)] = (T_NUMBER, style, mbf(v))

    def date(self, row, col, d, style=0):
        self.cells[(row, col)] = (T_DATE, style, mbf((d - EPOCH).days))

    def boolean(self, row, col, v, style=0):
        self.cells[(row, col)] = (T_BOOLEAN, style, mbf(1 if v else 0))

    def text(self, row, col, s, style=0):
        self.cells[(row, col)] = (T_TEXT, style, s)      # id filled in later

    def formula(self, row, col, src, style=0):
        self.cells[(row, col)] = (T_FORMULA, style, src) # id filled in later


class Book:
    def __init__(self):
        self.sheets = []
        self.styles = [bytes([NF_GENERAL, 0, 0, 0, 0])]   # id 0 is plain

    def sheet(self, name, widths=None):
        s = Sheet(name, widths)
        self.sheets.append(s)
        return s

    def style(self, fmt=NF_GENERAL, fg=0, bg=0, flags=0, places=0):
        st = bytes([fmt, fg, bg, flags, places])
        if st not in self.styles:
            self.styles.append(st)
        return self.styles.index(st)

    def write(self, path):
        # Every text and formula cell needs a pool id. Ids start at 1;
        # identical strings are NOT merged, because the reader rebuilds the
        # pool exactly as written and nothing here depends on sharing.
        strings = []
        for sh in self.sheets:
            for key in sorted(sh.cells):
                t, style, val = sh.cells[key]
                if t in (T_TEXT, T_FORMULA):
                    strings.append(val.encode("ascii", "replace"))
                    sh.cells[key] = (t, style, struct.pack("<H", len(strings))
                                     + b"\0\0\0")

        out = bytearray()
        out += b"X16S" + struct.pack("<HH", VERSION, 0)

        def chunk(tag, payload):
            out.extend(tag.encode())
            out.extend(struct.pack("<I", len(payload)))
            out.extend(payload)

        active = self.sheets[0]
        max_row = max((r for (r, _) in active.cells), default=0)
        max_col = max((c for (_, c) in active.cells), default=0)
        chunk("META", struct.pack("<HHHH", len(self.sheets), 0,
                                  max_row, max_col))

        chunk("SHNM", b"".join(s.name.encode("ascii", "replace") + b"\0"
                               for s in self.sheets))

        chunk("STYL", struct.pack("<H", len(self.styles))
                      + b"".join(self.styles))

        strs = struct.pack("<H", len(strings))
        for i, sdata in enumerate(strings):
            strs += struct.pack("<HH", i + 1, len(sdata)) + sdata
        chunk("STRS", strs)

        for n, sh in enumerate(self.sheets):
            widths = bytes(sh.widths.get(c, 10) for c in range(256))
            chunk("COLW", bytes([n]) + widths)

            rows = {}
            for (r, c), cell in sh.cells.items():
                rows.setdefault(r, {})[c] = cell
            body = bytes([n]) + struct.pack("<I", len(sh.cells))
            for r in sorted(rows):
                body += struct.pack("<HH", r, len(rows[r]))
                for c in sorted(rows[r]):
                    t, style, val = rows[r][c]
                    body += bytes([c, t, style]) + val
            chunk("CELS", body)

        chunk("END ", b"")
        out += struct.pack("<I", zlib.crc32(bytes(out)) & 0xFFFFFFFF)
        open(path, "wb").write(out)
        cells = sum(len(s.cells) for s in self.sheets)
        print("%-14s %d sheet(s), %4d cells, %d strings, %d bytes"
              % (os.path.basename(path), len(self.sheets), cells,
                 len(strings), len(out)))


# --------------------------------------------------------------------------
def invoice(b):
    """A one-sheet invoice: money, a computed line total, a percentage."""
    money = b.style(NF_CURRENCY, places=2)
    pct   = b.style(NF_PERCENT)
    dt    = b.style(NF_DATE)

    s = b.sheet("Invoice", [22, 8, 12, 12])
    s.text(0, 0, "Bramley & Sons")
    s.text(1, 0, "Invoice date");   s.date(1, 1, datetime.date(2026, 3, 14), dt)
    s.text(3, 0, "Item"); s.text(3, 1, "Qty")
    s.text(3, 2, "Each"); s.text(3, 3, "Total")
    items = [("Oak board 2m", 6, 24.50), ("Brass hinge", 24, 3.75),
             ("Wood screws 100", 3, 8.20), ("Danish oil 1L", 2, 18.95),
             ("Sandpaper pack", 5, 4.40)]
    r = 4
    for name, qty, each in items:
        s.text(r, 0, name)
        s.num(r, 1, qty)
        s.num(r, 2, each, money)
        s.formula(r, 3, "=B%d*C%d" % (r + 1, r + 1), money)
        r += 1
    s.text(r, 0, "Subtotal")
    s.formula(r, 3, "=SUM(D5:D%d)" % r, money)
    s.text(r + 1, 0, "VAT")
    s.num(r + 1, 1, 0.2, pct)
    s.formula(r + 1, 3, "=D%d*B%d" % (r + 1, r + 2), money)
    s.text(r + 2, 0, "Due")
    s.formula(r + 2, 3, "=D%d+D%d" % (r + 1, r + 2), money)


def rainfall(b):
    """Two sheets, the second reading the first across a sheet boundary."""
    two = b.style(NF_DECIMAL, places=1)

    d = b.sheet("Readings", [10, 10, 10, 10])
    d.text(0, 0, "Month"); d.text(0, 1, "2024")
    d.text(0, 2, "2025");  d.text(0, 3, "Change")
    months = ("January", "February", "March", "April", "May", "June",
              "July", "August", "September", "October", "November",
              "December")
    a = [82.4, 61.0, 55.2, 48.9, 51.3, 44.7,
         39.8, 47.1, 62.5, 88.3, 95.0, 90.6]
    c = [70.1, 73.8, 44.0, 60.2, 38.7, 52.9,
         55.4, 41.0, 71.2, 79.9, 102.3, 84.1]
    for i, m in enumerate(months):
        d.text(i + 1, 0, m)
        d.num(i + 1, 1, a[i], two)
        d.num(i + 1, 2, c[i], two)
        d.formula(i + 1, 3, "=C%d-B%d" % (i + 2, i + 2), two)

    s = b.sheet("Summary", [14, 12])
    s.text(0, 0, "Rainfall, mm")
    s.text(2, 0, "2024 total");   s.formula(2, 1, "=SUM(Readings!B2:B13)", two)
    s.text(3, 0, "2025 total");   s.formula(3, 1, "=SUM(Readings!C2:C13)", two)
    s.text(4, 0, "Wettest 2025"); s.formula(4, 1, "=MAX(Readings!C2:C13)", two)
    s.text(5, 0, "Driest 2025");  s.formula(5, 1, "=MIN(Readings!C2:C13)", two)
    s.text(6, 0, "Mean 2025");    s.formula(6, 1, "=AVERAGE(Readings!C2:C13)", two)
    s.text(7, 0, "Months");       s.formula(7, 1, "=COUNT(Readings!C2:C13)")
    s.text(9, 0, "Wetter year")
    s.formula(9, 1, "=IF(B4>B3,2025,2024)")


def kinds(b):
    """One of every cell type and number format, for reading down."""
    s = b.sheet("Kinds", [16, 16, 26])
    s.text(0, 0, "Kind"); s.text(0, 1, "Value"); s.text(0, 2, "Note")
    rows = [
        ("Text",     lambda r: s.text(r, 1, "hello"),          "left by default"),
        ("Number",   lambda r: s.num(r, 1, 1234.5),            "right by default"),
        ("Negative", lambda r: s.num(r, 1, -87.25),            ""),
        ("Tiny",     lambda r: s.num(r, 1, 0.001),             ""),
        ("Currency", lambda r: s.num(r, 1, 1999.99,
                                     b.style(NF_CURRENCY, places=2)), ""),
        ("Percent",  lambda r: s.num(r, 1, 0.075,
                                     b.style(NF_PERCENT)),    "stored as 0.075"),
        ("Fixed 3",  lambda r: s.num(r, 1, 3.14159,
                                     b.style(NF_DECIMAL, places=3)), ""),
        ("Date",     lambda r: s.date(r, 1, datetime.date(2026, 8, 6),
                                      b.style(NF_DATE)),      ""),
        ("Boolean",  lambda r: s.boolean(r, 1, True),          ""),
        ("Formula",  lambda r: s.formula(r, 1, "=2^10"),       "should read 1024"),
        ("Error",    lambda r: s.formula(r, 1, "=1/0"),        "should read #DIV/0!"),
    ]
    for i, (label, put, note) in enumerate(rows):
        r = i + 1
        s.text(r, 0, label)
        put(r)
        if note:
            s.text(r, 2, note)


def budget(b):
    """A household budget: the sheet most people would actually type.

    Deliberately chart-shaped -- a column of labels beside a column of
    numbers, which is exactly what Chart bars and Chart pie read."""
    money = b.style(NF_CURRENCY, places=2)
    pct   = b.style(NF_PERCENT)

    s = b.sheet("Budget", [16, 12, 10, 14])
    s.text(0, 0, "Monthly outgoings")
    s.text(2, 0, "What"); s.text(2, 1, "Amount"); s.text(2, 3, "Share")

    rows = [("Rent", 875.00), ("Food", 310.50), ("Power", 96.20),
            ("Transport", 142.00), ("Phone", 28.00), ("Insurance", 63.40),
            ("Savings", 250.00), ("Other", 84.90)]
    r = 3
    for what, amount in rows:
        s.text(r, 0, what)
        s.num(r, 1, amount, money)
        # Each row's share of the total, which is what a pie chart shows.
        s.formula(r, 3, "=B%d/$B$12" % (r + 1), pct)
        r += 1

    s.text(r, 0, "Total")
    s.formula(r, 1, "=SUM(B4:B%d)" % r, money)
    s.text(r + 2, 0, "Biggest")
    s.formula(r + 2, 1, "=MAX(B4:B%d)" % r, money)
    s.text(r + 3, 0, "Smallest")
    s.formula(r + 3, 1, "=MIN(B4:B%d)" % r, money)
    s.text(r + 4, 0, "Average")
    s.formula(r + 4, 1, "=AVERAGE(B4:B%d)" % r, money)


def sales(b):
    """Twelve months by four regions: wide enough to need scrolling, and
    the shape Freeze was written for -- headings across the top and down
    the left, with the data between them."""
    money = b.style(NF_CURRENCY, places=0)

    s = b.sheet("Sales", [12, 11, 11, 11, 11, 13])
    regions = ("North", "South", "East", "West")
    s.text(0, 0, "Month")
    for i, name in enumerate(regions):
        s.text(0, i + 1, name)
    s.text(0, 5, "Total")

    months = ("January", "February", "March", "April", "May", "June",
              "July", "August", "September", "October", "November",
              "December")
    base = [(41200, 38900, 27350, 33100), (39800, 41100, 29200, 31700),
            (46300, 44050, 31900, 38400), (44100, 42600, 30150, 36950),
            (48750, 45300, 34600, 41200), (52400, 49850, 36100, 43700),
            (50900, 47200, 35400, 42150), (47600, 44900, 33050, 39800),
            (53100, 51200, 38700, 45300), (56800, 54100, 41200, 48600),
            (61400, 58300, 44950, 52100), (67200, 63800, 49100, 57400)]
    for i, month in enumerate(months):
        r = i + 1
        s.text(r, 0, month)
        for c, v in enumerate(base[i]):
            s.num(r, c + 1, v, money)
        s.formula(r, 5, "=SUM(B%d:E%d)" % (r + 1, r + 1), money)

    s.text(13, 0, "Year")
    for c in range(1, 6):
        col = "BCDEF"[c - 1]
        s.formula(13, c, "=SUM(%s2:%s13)" % (col, col), money)


def sorting(b):
    """Unsorted on purpose, with a heading row -- for trying Data > Sort up
    with the cursor on the first row of the DATA, and Undo sort after."""
    s = b.sheet("Parts", [10, 20, 8, 10])
    s.text(0, 0, "Code"); s.text(0, 1, "Description")
    s.text(0, 2, "Qty");  s.text(0, 3, "Bin")

    parts = [("M8-40", "Hex bolt M8 x 40", 120, "A3"),
             ("W-08", "Washer 8mm", 940, "A1"),
             ("N-08", "Nyloc nut M8", 310, "A2"),
             ("M6-25", "Hex bolt M6 x 25", 85, "B1"),
             ("SPR-3", "Spring 30mm", 42, "C4"),
             ("W-06", "Washer 6mm", 1200, "A1"),
             ("GRUB-4", "Grub screw M4", 6, "C1"),
             ("M10-60", "Hex bolt M10 x 60", 57, "B4"),
             ("CIRC-12", "Circlip 12mm", 208, "C2"),
             ("N-06", "Nyloc nut M6", 430, "A2")]
    for i, (code, desc, qty, bin_) in enumerate(parts):
        r = i + 1
        s.text(r, 0, code)
        s.text(r, 1, desc)
        s.num(r, 2, qty)
        s.text(r, 3, bin_)


def nasdaq(b, outdir):
    """The same 500 days make_nasdaq.py writes as .xlsx, natively.

    The point of this one is the clock. An .xlsx has to be unzipped,
    inflated and parsed before a single cell exists; a .X16S is read
    straight into the cell store."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from make_nasdaq import fetch, EPOCH as NEPOCH
    data = fetch(outdir)
    keys = sorted(data)[-500:]

    dt  = b.style(NF_DATE)
    two = b.style(NF_DECIMAL, places=2)
    thou = b.style(NF_INTEGER)

    p = b.sheet("Prices", [12, 11, 11, 11, 11, 13])
    for i, t in enumerate(("Date", "Open", "High", "Low", "Close", "Vol")):
        p.text(0, i, t)
    for i, k in enumerate(keys):
        o, h, l, c, v = data[k]
        r = i + 1
        p.date(r, 0, datetime.date.fromtimestamp(k), dt)
        p.num(r, 1, round(o, 2), two); p.num(r, 2, round(h, 2), two)
        p.num(r, 3, round(l, 2), two); p.num(r, 4, round(c, 2), two)
        p.num(r, 5, int((v or 0) / 1000), thou)

    years = {}
    for i, k in enumerate(keys):
        years.setdefault(datetime.date.fromtimestamp(k).year, []).append(i + 2)

    s = b.sheet("Summary", [8, 12, 12, 12])
    for i, t in enumerate(("Year", "High", "Low", "Days")):
        s.text(0, i, t)
    r = 1
    for y in sorted(years):
        lo, hi = years[y][0], years[y][-1]
        s.num(r, 0, y)
        s.formula(r, 1, "=MAX(Prices!C%d:C%d)" % (lo, hi), two)
        s.formula(r, 2, "=MIN(Prices!D%d:D%d)" % (lo, hi), two)
        s.formula(r, 3, "=COUNT(Prices!E%d:E%d)" % (lo, hi))
        r += 1


def main(outdir):
    b = Book()
    nasdaq(b, outdir)
    b.write(os.path.join(outdir, "NDQNAT.X16S"))
    for name, build in (("INVOICE.X16S", invoice),
                        ("RAIN.X16S", rainfall),
                        ("KINDS.X16S", kinds),
                        ("BUDGET.X16S", budget),
                        ("SALES.X16S", sales),
                        ("PARTS.X16S", sorting)):
        b = Book()
        build(b)
        b.write(os.path.join(outdir, name))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build")
