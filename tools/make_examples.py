#!/usr/bin/env python3
"""make_examples — a set of workbooks to open on the machine.

Not fixtures. DEMO.XLSX is the fixture the importer is measured against and
SHEETS.XLSX is for eyeballing sheet switching; both are built to be awkward.
These are built to be USED: each one is a plausible small spreadsheet that
exercises a part of the program, so that opening them is a way of finding
out whether it works rather than whether it parses.

    python3 tools/make_examples.py <dir>

Every formula here is one x16sheet can compile. The importer keeps Excel's
cached value for anything it cannot translate, so a workbook full of
unsupported functions would look right and prove nothing -- the point of
these is that the numbers on screen were worked out ON the X16.
"""
import sys, zipfile

# Number formats, in the order they are written into styles.xml. Index 0 is
# General and every cell that names no style gets it.
FORMATS = [
    "General",
    '"$"#,##0.00',      # 1 money
    "0%",               # 2 percentage
    "yyyy\\-mm\\-dd",   # 3 date
    "0.00",             # 4 two decimals
    "#,##0",            # 5 thousands
]

# One more xf after the formats, using the bold font. Appended rather than
# inserted so every existing style index keeps its meaning.
BOLD = len(FORMATS)

def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))

class Sheet:
    def __init__(self, name, widths=None):
        self.name = name
        self.widths = widths or []
        self.rows = {}          # row -> {col -> (kind, value, style)}

    def _put(self, ref, cell):
        col = "".join(c for c in ref if c.isalpha())
        row = int("".join(c for c in ref if c.isdigit()))
        n = 0
        for c in col:
            n = n * 26 + (ord(c.upper()) - 64)
        self.rows.setdefault(row, {})[n] = (ref,) + cell

    def text(self, ref, s, style=0):    self._put(ref, ("t", s, style))
    def num(self, ref, v, style=0):     self._put(ref, ("n", v, style))
    def formula(self, ref, f, cached, style=0):
        self._put(ref, ("f", (f, cached), style))

    def xml(self):
        x = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
             '<worksheet xmlns="http://schemas.openxmlformats.org/'
             'spreadsheetml/2006/main">']
        if self.widths:
            x.append("<cols>")
            for i, w in enumerate(self.widths):
                x.append('<col min="%d" max="%d" width="%d" customWidth="1"/>'
                         % (i + 1, i + 1, w))
            x.append("</cols>")
        x.append("<sheetData>")
        for r in sorted(self.rows):
            x.append('<row r="%d">' % r)
            for c in sorted(self.rows[r]):
                ref, kind, val, style = self.rows[r][c]
                s = ' s="%d"' % style if style else ""
                if kind == "t":
                    x.append('<c r="%s"%s t="inlineStr"><is><t>%s</t></is></c>'
                             % (ref, s, esc(str(val))))
                elif kind == "n":
                    x.append('<c r="%s"%s><v>%s</v></c>' % (ref, s, val))
                else:
                    f, cached = val
                    x.append('<c r="%s"%s><f>%s</f><v>%s</v></c>'
                             % (ref, s, esc(f), cached))
            x.append("</row>")
        x.append("</sheetData></worksheet>")
        return "".join(x)


def write(path, sheets, compress=False):
    """STORED entries by default; compress=True to deflate instead.

    Worth knowing about: inflating is 54% of the time it takes the X16 to
    open a workbook -- 63 of 117 seconds for 500 days of prices, at about
    1.9 KB a second -- and the reader skips that phase entirely for a
    stored entry. The file is four or five times larger on the card and
    opens in half the time. Every .xlsx x16sheet writes is stored for the
    same reason.
    """
    z = zipfile.ZipFile(path, "w",
                        zipfile.ZIP_DEFLATED if compress else zipfile.ZIP_STORED)
    n = len(sheets)

    over = "".join('<Override PartName="/xl/worksheets/sheet%d.xml" ContentType='
                   '"application/vnd.openxmlformats-officedocument.'
                   'spreadsheetml.worksheet+xml"/>' % (i + 1) for i in range(n))
    z.writestr("[Content_Types].xml",
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.'
        'openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>'
        '<Override PartName="/xl/styles.xml" ContentType="application/vnd.'
        'openxmlformats-officedocument.spreadsheetml.styles+xml"/>'
        + over + "</Types>")

    z.writestr("_rels/.rels",
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/'
        'officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/>'
        "</Relationships>")

    tabs = "".join('<sheet name="%s" sheetId="%d" r:id="rId%d"/>'
                   % (s.name, i + 1, i + 1) for i, s in enumerate(sheets))
    z.writestr("xl/workbook.xml",
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        "<sheets>" + tabs + "</sheets></workbook>")

    rels = "".join('<Relationship Id="rId%d" Type="http://schemas.openxmlformats.org/'
                   'officeDocument/2006/relationships/worksheet" '
                   'Target="worksheets/sheet%d.xml"/>' % (i + 1, i + 1)
                   for i in range(n))
    rels += ('<Relationship Id="rId%d" Type="http://schemas.openxmlformats.org/'
             'officeDocument/2006/relationships/styles" Target="styles.xml"/>' % (n + 1))
    z.writestr("xl/_rels/workbook.xml.rels",
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        + rels + "</Relationships>")

    # formatCode lives in an XML attribute and a money format contains
    # double quotes -- Excel writes them as &quot; and so must we, or the
    # styles part is not well-formed XML at all.
    fmts = "".join('<numFmt numFmtId="%d" formatCode="%s"/>'
                   % (164 + i, f.replace('"', "&quot;"))
                   for i, f in enumerate(FORMATS))
    xfs = "".join('<xf numFmtId="%d" fontId="0" fillId="0" borderId="0" '
                  'applyNumberFormat="1"/>' % (164 + i)
                  for i in range(len(FORMATS)))
    # Style BOLD: General, but the bold font. fontId is what the importer
    # follows back to the <b/>.
    xfs += ('<xf numFmtId="0" fontId="1" fillId="0" borderId="0" '
            'applyFont="1"/>')
    z.writestr("xl/styles.xml",
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">'
        '<numFmts count="%d">%s</numFmts>'
        '<fonts count="2">'
        '<font><sz val="11"/><name val="Calibri"/></font>'
        '<font><b/><sz val="11"/><name val="Calibri"/></font>'
        '</fonts>'
        '<fills count="1"><fill><patternFill patternType="none"/></fill></fills>'
        '<borders count="1"><border/></borders>'
        '<cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>'
        '<cellXfs count="%d">%s</cellXfs></styleSheet>'
        % (len(FORMATS), fmts, len(FORMATS) + 1, xfs))

    for i, s in enumerate(sheets):
        z.writestr("xl/worksheets/sheet%d.xml" % (i + 1), s.xml())
    z.close()


# --------------------------------------------------------------------------
# BUDGET.XLSX — money, percentages, and a total that has to add up.
# --------------------------------------------------------------------------
def budget():
    s = Sheet("Budget", [18, 12, 12, 12, 10])
    # The heading row is bold, which is also the importer's only fixture
    # for reading a <b/> font back out of styles.xml.
    s.text("A1", "Category", BOLD); s.text("B1", "Budget", BOLD)
    s.text("C1", "Actual",   BOLD); s.text("D1", "Left",   BOLD)
    s.text("E1", "Used",     BOLD)
    items = [("Rent", 1200, 1200), ("Groceries", 400, 372.4),
             ("Transport", 120, 96.5), ("Utilities", 180, 203.1),
             ("Phone", 35, 35), ("Internet", 45, 45),
             ("Dining out", 150, 218.75), ("Clothing", 80, 42),
             ("Healthcare", 90, 60), ("Savings", 500, 500)]
    r = 2
    for name, b, a in items:
        s.text("A%d" % r, name)
        s.num("B%d" % r, b, 1)
        s.num("C%d" % r, a, 1)
        s.formula("D%d" % r, "B%d-C%d" % (r, r), round(b - a, 2), 1)
        s.formula("E%d" % r, "C%d/B%d" % (r, r), round(a / b, 4), 2)
        r += 1
    s.text("A%d" % r, "Total")
    s.formula("B%d" % r, "SUM(B2:B%d)" % (r - 1), sum(i[1] for i in items), 1)
    s.formula("C%d" % r, "SUM(C2:C%d)" % (r - 1), round(sum(i[2] for i in items), 2), 1)
    s.formula("D%d" % r, "B%d-C%d" % (r, r),
              round(sum(i[1] for i in items) - sum(i[2] for i in items), 2), 1)
    r += 1
    s.text("A%d" % r, "Biggest")
    s.formula("C%d" % r, "MAX(C2:C%d)" % (r - 2), max(i[2] for i in items), 1)
    r += 1
    s.text("A%d" % r, "Average")
    s.formula("C%d" % r, "AVERAGE(C2:C%d)" % (r - 3),
              round(sum(i[2] for i in items) / len(items), 2), 1)
    return [s]


# --------------------------------------------------------------------------
# STOCK.XLSX — the one to try sorting. Deliberately in no order at all.
# --------------------------------------------------------------------------
def stock():
    s = Sheet("Stock", [8, 16, 8, 10, 12])
    s.text("A1", "Code"); s.text("B1", "Item"); s.text("C1", "Qty")
    s.text("D1", "Price"); s.text("E1", "Value")
    items = [("K42", "Bracket", 14, 2.5), ("A07", "Washer", 500, 0.04),
             ("Z19", "Sensor", 3, 48.0), ("M33", "Cable", 62, 1.75),
             ("B88", "Relay", 8, 12.4), ("Q05", "Fuse", 240, 0.35),
             ("T61", "Spring", 95, 0.6), ("C14", "Gasket", 27, 3.2),
             ("X902", "Bearing", 6, 22.9), ("D50", "Clip", 310, 0.12),
             ("R77", "Switch", 19, 7.85), ("F23", "Diode", 400, 0.09)]
    r = 2
    for code, name, qty, price in items:
        s.text("A%d" % r, code)
        s.text("B%d" % r, name)
        s.num("C%d" % r, qty, 5)
        s.num("D%d" % r, price, 1)
        s.formula("E%d" % r, "C%d*D%d" % (r, r), round(qty * price, 2), 1)
        r += 1
    s.text("A%d" % r, "Total")
    s.formula("E%d" % r, "SUM(E2:E%d)" % (r - 1),
              round(sum(q * p for _, _, q, p in items), 2), 1)
    r += 1
    s.text("A%d" % r, "Lines")
    s.formula("C%d" % r, "COUNT(C2:C%d)" % (r - 2), len(items), 5)
    return [s]


# --------------------------------------------------------------------------
# TOTALS.XLSX — three sheets, and the third reads the other two.
# The feature that had nowhere to be demonstrated until now.
# --------------------------------------------------------------------------
def totals():
    q1 = Sheet("Q1", [14, 12])
    q2 = Sheet("Q2", [14, 12])
    for sh, base in ((q1, 100), (q2, 130)):
        sh.text("A1", "Month"); sh.text("B1", "Sales")
        months = (("January", "February", "March") if sh is q1
                  else ("April", "May", "June"))
        for i, m in enumerate(months):
            sh.text("A%d" % (i + 2), m)
            sh.num("B%d" % (i + 2), base + i * 17, 1)
        sh.text("A5", "Total")
        sh.formula("B5", "SUM(B2:B4)", base * 3 + 51, 1)

    sm = Sheet("Summary", [16, 12, 10])
    sm.text("A1", "Half year")
    sm.text("A3", "Q1");    sm.formula("B3", "Q1!B5", 351, 1)
    sm.text("A4", "Q2");    sm.formula("B4", "Q2!B5", 441, 1)
    sm.text("A5", "Total")
    sm.formula("B5", "Q1!B5+Q2!B5", 792, 1)
    sm.text("A7", "Best month")
    sm.formula("B7", "MAX(Q1!B2:B4,Q2!B2:B4)", 164, 1)
    sm.text("A8", "Months")
    sm.formula("B8", "COUNT(Q1!B2:B4,Q2!B2:B4)", 6)
    sm.text("A9", "Average")
    sm.formula("B9", "AVERAGE(Q1!B2:B4,Q2!B2:B4)", 132, 1)
    return [q1, q2, sm]


# --------------------------------------------------------------------------
# FUNCS.XLSX — one row per function, each next to what it should say.
# A page you can read down to see whether the evaluator agrees.
# --------------------------------------------------------------------------
def funcs():
    s = Sheet("Functions", [14, 16, 14, 22])
    s.text("A1", "Function"); s.text("B1", "Result")
    s.text("C1", "Should be"); s.text("D1", "Note")
    rows = [
        ("SUM(A20:A24)",      "SUM", 150, "150", "adds a range"),
        ("AVERAGE(A20:A24)",  "AVERAGE", 30, "30", "empty cells skipped"),
        ("MIN(A20:A24)",      "MIN", 10, "10", ""),
        ("MAX(A20:A24)",      "MAX", 50, "50", ""),
        ("COUNT(A20:A24)",    "COUNT", 5, "5", "counts numbers only"),
        ("ABS(0-7)",          "ABS", 7, "7", ""),
        ("INT(7.8)",          "INT", 7, "7", "towards zero"),
        ("ROUND(2.345,2)",    "ROUND", 2.35, "2.35", ""),
        ("MOD(17,5)",         "MOD", 2, "2", ""),
        ("2^10",              "power", 1024, "1024", ""),
        ("IF(A20>5,1,0)",     "IF", 1, "1", "true branch"),
        ("AND(1,1)",          "AND", "TRUE", "TRUE", ""),
        ("OR(0,1)",           "OR", "TRUE", "TRUE", ""),
        ("NOT(0)",            "NOT", "TRUE", "TRUE", ""),
        ("LEN(\"spreadsheet\")", "LEN", 11, "11", ""),
        ("A20>A21",           "compare", "FALSE", "FALSE", "10 > 20 is false"),
        ("A20/0",             "divide by 0", "#DIV/0!", "#DIV/0!", "an error, on purpose"),
    ]
    r = 2
    for f, label, cached, want, note in rows:
        s.text("A%d" % r, label)
        if isinstance(cached, str) and not cached.replace(".", "").isdigit():
            # booleans and errors: let the machine work them out
            s.formula("B%d" % r, f, 0)
        else:
            s.formula("B%d" % r, f, cached)
        s.text("C%d" % r, want)
        if note:
            s.text("D%d" % r, note)
        r += 1

    s.text("A19", "Inputs")
    for i, v in enumerate((10, 20, 30, 40, 50)):
        s.num("A%d" % (20 + i), v)
    return [s]


EXAMPLES = [
    ("BUDGET.XLSX", budget, "money, percentages and totals"),
    ("STOCK.XLSX",  stock,  "unsorted -- try Sheet > Sort up on any column"),
    ("TOTALS.XLSX", totals, "three sheets; Summary reads Q1 and Q2"),
    ("FUNCS.XLSX",  funcs,  "every function, beside what it should say"),
]

def main(outdir):
    out = outdir.rstrip("/")
    for name, build, what in EXAMPLES:
        sheets = build()
        write(out + "/" + name, sheets)
        cells = sum(len(r) for s in sheets for r in s.rows.values())
        print("%-13s %d sheet(s), %3d cells  -- %s"
              % (name, len(sheets), cells, what))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build")
