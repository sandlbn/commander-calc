#!/usr/bin/env python3
"""Build DEMO.XLSX — the acceptance workbook from the plan, section 35.

A household budget, an inventory and a summary that references both, with
every cell type and number format the importer is meant to handle. Written
as raw XML rather than through a library so the exact shapes Excel emits are
under our control — including cached formula results, which is what an
importer that cannot evaluate a formula has to fall back on.

Large enough to matter: the worksheet parts run to tens of kilobytes, so
the streaming paths are doing real work rather than fitting in one buffer.
"""
import sys, zipfile, datetime

out = sys.argv[1] if len(sys.argv) > 1 else "build/host/sd"
HDR = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'
R = 'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"'

# ---------------------------------------------------------------- strings
strings = []
def si(text):
    """Intern a string, returning its shared index."""
    if text not in strings:
        strings.append(text)
    return strings.index(text)

# ---------------------------------------------------------------- styles
#
# Style indices used below. numFmtId picks the format; the builtin ids are
# the ones Excel does not write out (0 General, 9 percent, 14 date, 44
# currency), and 164+ are custom.
#
#   0 General   1 currency   2 percent   3 date   4 integer
#   5 two-decimal            6 bold text (General)
STYLES = HDR + '''<styleSheet>
<numFmts count="2">
  <numFmt numFmtId="164" formatCode="&quot;$&quot;#,##0.00"/>
  <numFmt numFmtId="165" formatCode="yyyy\\-mm\\-dd"/>
</numFmts>
<fonts count="2"><font><sz val="11"/><name val="Calibri"/></font>
  <font><b/><sz val="11"/><name val="Calibri"/></font></fonts>
<fills count="2"><fill><patternFill patternType="none"/></fill>
  <fill><patternFill patternType="solid"><fgColor rgb="FFD9E1F2"/>
  </patternFill></fill></fills>
<borders count="1"><border/></borders>
<cellStyleXfs count="1"><xf numFmtId="0" fontId="0"/></cellStyleXfs>
<cellXfs count="7">
  <xf numFmtId="0"   fontId="0" xfId="0"/>
  <xf numFmtId="164" fontId="0" xfId="0" applyNumberFormat="1"/>
  <xf numFmtId="9"   fontId="0" xfId="0" applyNumberFormat="1"/>
  <xf numFmtId="165" fontId="0" xfId="0" applyNumberFormat="1"/>
  <xf numFmtId="1"   fontId="0" xfId="0" applyNumberFormat="1"/>
  <xf numFmtId="2"   fontId="0" xfId="0" applyNumberFormat="1"/>
  <xf numFmtId="0"   fontId="1" xfId="0" applyFont="1" fillId="1"/>
</cellXfs>
</styleSheet>'''

S_GEN, S_CUR, S_PCT, S_DATE, S_INT, S_DEC, S_BOLD = range(7)

def col(n):
    """0 -> A, 25 -> Z, 26 -> AA."""
    s = ""
    while True:
        s = chr(ord('A') + n % 26) + s
        if n < 26:
            return s
        n = n // 26 - 1

def cell(c, r, value=None, kind=None, style=0, formula=None):
    """One <c>. `value` is the cached result when there is a formula."""
    a = f'r="{col(c)}{r}"'
    if style:
        a += f' s="{style}"'
    if kind:
        a += f' t="{kind}"'
    inner = ""
    if formula is not None:
        inner += f"<f>{formula}</f>"
    if value is not None:
        if kind == "inlineStr":
            return f'<c {a}><is><t>{value}</t></is></c>'
        inner += f"<v>{value}</v>"
    return f"<c {a}>{inner}</c>" if inner else f"<c {a}/>"

def sheet_xml(rows, dim, cols=None):
    colspec = ""
    if cols:
        colspec = "<cols>" + "".join(
            f'<col min="{a}" max="{b}" width="{w}" customWidth="1"/>'
            for a, b, w in cols) + "</cols>"
    return (HDR + f'<worksheet {R}><dimension ref="{dim}"/>'
            '<sheetViews><sheetView workbookViewId="0"/></sheetViews>'
            f'{colspec}<sheetData>{rows}</sheetData></worksheet>')

def epoch(y, m, d):
    """Excel's 1900 serial, including the phantom 1900-02-29."""
    return (datetime.date(y, m, d) - datetime.date(1899, 12, 31)).days + 1

# ------------------------------------------------------------ sheet 1
# A household budget: labels, currency, percentages, dates, and the three
# formulas the plan names — SUM, AVERAGE, IF.
CATEGORIES = [
    ("Rent", 1200.00, 0.35), ("Groceries", 480.50, 0.14),
    ("Utilities", 210.75, 0.06), ("Transport", 165.00, 0.05),
    ("Insurance", 320.00, 0.09), ("Phone", 45.99, 0.01),
    ("Internet", 59.99, 0.02), ("Dining out", 235.40, 0.07),
    ("Clothing", 128.00, 0.04), ("Healthcare", 190.25, 0.06),
    ("Savings", 400.00, 0.12), ("Misc", 95.30, 0.03),
]

rows = []
rows.append('<row r="1" ht="18" customHeight="1">' + "".join([
    cell(0, 1, si("Category"),  "s", S_BOLD),
    cell(1, 1, si("Budgeted"),  "s", S_BOLD),
    cell(2, 1, si("Actual"),    "s", S_BOLD),
    cell(3, 1, si("Variance"),  "s", S_BOLD),
    cell(4, 1, si("Share"),     "s", S_BOLD),
    cell(5, 1, si("Reviewed"),  "s", S_BOLD),
    cell(6, 1, si("Over?"),     "s", S_BOLD),
]) + "</row>")

for i, (name, amount, share) in enumerate(CATEGORIES):
    r = i + 2
    actual = round(amount * (1.04 if i % 3 == 0 else 0.97), 2)
    rows.append(f'<row r="{r}">' + "".join([
        cell(0, r, si(name), "s"),
        cell(1, r, f"{amount}", None, S_CUR),
        cell(2, r, f"{actual}", None, S_CUR),
        # a formula with its cached result, which is what we display until
        # the engine can recalculate it
        cell(3, r, f"{round(actual - amount, 2)}", None, S_CUR,
             formula=f"C{r}-B{r}"),
        cell(4, r, f"{share}", None, S_PCT),
        cell(5, r, str(epoch(2026, 1 + i % 12, 1 + i % 28)), None, S_DATE),
        cell(6, r, "1" if actual > amount else "0", "b", S_GEN,
             formula=f"C{r}&gt;B{r}"),
    ]) + "</row>")

n = len(CATEGORIES) + 1
total_b = round(sum(c[1] for c in CATEGORIES), 2)
total_a = 0.0
for i, (nm, amount, sh) in enumerate(CATEGORIES):
    total_a += round(amount * (1.04 if i % 3 == 0 else 0.97), 2)
total_a = round(total_a, 2)

rows.append(f'<row r="{n+1}">' + "".join([
    cell(0, n+1, si("Total"), "s", S_BOLD),
    cell(1, n+1, f"{total_b}", None, S_CUR, formula=f"SUM(B2:B{n})"),
    cell(2, n+1, f"{total_a}", None, S_CUR, formula=f"SUM(C2:C{n})"),
    cell(3, n+1, f"{round(total_a-total_b,2)}", None, S_CUR,
         formula=f"SUM(D2:D{n})"),
]) + "</row>")
rows.append(f'<row r="{n+2}">' + "".join([
    cell(0, n+2, si("Average"), "s", S_BOLD),
    cell(1, n+2, f"{round(total_b/len(CATEGORIES),2)}", None, S_CUR,
         formula=f"AVERAGE(B2:B{n})"),
    cell(2, n+2, f"{round(total_a/len(CATEGORIES),2)}", None, S_CUR,
         formula=f"AVERAGE(C2:C{n})"),
]) + "</row>")
rows.append(f'<row r="{n+3}">' + "".join([
    cell(0, n+3, si("Status"), "s", S_BOLD),
    cell(1, n+3, si("Over budget" if total_a > total_b else "On track"), "s",
         S_GEN, formula=f'IF(C{n+1}&gt;B{n+1},"Over budget","On track")'),
]) + "</row>")
# the cases an importer has to survive rather than understand
rows.append(f'<row r="{n+5}">' + "".join([
    cell(0, n+5, si("Edge cases"), "s", S_BOLD),
    cell(1, n+5, "#DIV/0!", "e", S_GEN, formula="1/0"),
    cell(2, n+5, "0", None, S_GEN, formula="COUNTBLANK(A1:A2)"),
    cell(3, n+5, "inline text", "inlineStr"),
    cell(4, n+5, si(""), "s"),
    cell(6, n+5, "-0.5", None, S_DEC),
]) + "</row>")

SHEET1 = sheet_xml("".join(rows), f"A1:G{n+5}",
                   cols=[(1, 1, 18.5), (2, 5, 12.0), (6, 6, 14.0)])

# ------------------------------------------------------------ sheet 2
# An inventory, long enough that the worksheet part is tens of kilobytes.
ITEMS = ["Cable", "Adapter", "Bracket", "Screw", "Washer", "Fuse", "Relay",
         "Switch", "Diode", "Resistor", "Capacitor", "Inductor", "Sensor",
         "Bearing", "Gasket", "Seal", "Spring", "Pin", "Clip", "Grommet"]

rows = ['<row r="1">' + "".join([
    cell(0, 1, si("SKU"),      "s", S_BOLD),
    cell(1, 1, si("Item"),     "s", S_BOLD),
    cell(2, 1, si("Quantity"), "s", S_BOLD),
    cell(3, 1, si("Price"),    "s", S_BOLD),
    cell(4, 1, si("Value"),    "s", S_BOLD),
    cell(5, 1, si("In stock"), "s", S_BOLD),
]) + "</row>"]

for i in range(200):
    r = i + 2
    item = ITEMS[i % len(ITEMS)]
    qty = (i * 7) % 95
    price = round(1.25 + (i % 40) * 0.37, 2)
    rows.append(f'<row r="{r}">' + "".join([
        cell(0, r, f"SKU-{1000+i}", "inlineStr"),
        cell(1, r, si(item), "s"),
        cell(2, r, str(qty), None, S_INT),
        cell(3, r, f"{price}", None, S_CUR),
        cell(4, r, f"{round(qty*price,2)}", None, S_CUR,
             formula=f"C{r}*D{r}"),
        cell(5, r, "1" if qty else "0", "b", S_GEN, formula=f"C{r}&gt;0"),
    ]) + "</row>")

rows.append(f'<row r="202">' + "".join([
    cell(1, 202, si("Total value"), "s", S_BOLD),
    cell(4, 202, "0", None, S_CUR, formula="SUM(E2:E201)"),
]) + "</row>")
SHEET2 = sheet_xml("".join(rows), "A1:F202")

# ------------------------------------------------------------ sheet 3
# A summary that reaches across sheets, plus the functions the engine
# supports and a few it does not.
rows = ['<row r="1">' + "".join([
    cell(0, 1, si("Summary"), "s", S_BOLD),
]) + "</row>"]
SUMMARY = [
    ("Budget total",    f"Budget!B{n+1}",        f"{total_b}",   S_CUR),
    ("Actual total",    f"Budget!C{n+1}",        f"{total_a}",   S_CUR),
    ("Inventory value", "Inventory!E202",        "0",            S_CUR),
    ("Largest expense", f"MAX(Budget!B2:B{n})",  "1200",         S_CUR),
    ("Smallest",        f"MIN(Budget!B2:B{n})",  "45.99",        S_CUR),
    ("Lines",           f"COUNT(Budget!B2:B{n})", str(len(CATEGORIES)), S_INT),
    ("Rounded",         f"ROUND(Budget!C{n+1},0)", str(round(total_a)), S_INT),
    ("Absolute",        f"ABS(Budget!D{n+1})",   "0",            S_CUR),
    # deliberately unsupported: must keep its text and its cached value
    ("Unsupported",     'VLOOKUP("Rent",Budget!A2:B13,2,FALSE)', "1200", S_CUR),
    ("Also unsupported", "TEXT(NOW(),\"yyyy\")", "2026",         S_GEN),
]
for i, (label, f, v, st) in enumerate(SUMMARY):
    r = i + 2
    rows.append(f'<row r="{r}">' + "".join([
        cell(0, r, si(label), "s"),
        cell(1, r, v, None, st, formula=f),
    ]) + "</row>")
SHEET3 = sheet_xml("".join(rows), "A1:B11")

# ---------------------------------------------------------------- package
SHARED = (HDR + f'<sst xmlns="http://schemas.openxmlformats.org/'
          f'spreadsheetml/2006/main" count="{len(strings)}" '
          f'uniqueCount="{len(strings)}">'
          + "".join(f"<si><t>{s}</t></si>" for s in strings) + "</sst>")

WORKBOOK = (HDR + f'<workbook {R}><sheets>'
            '<sheet name="Budget" sheetId="1" r:id="rId1"/>'
            '<sheet name="Inventory" sheetId="2" r:id="rId2"/>'
            '<sheet name="Summary" sheetId="3" r:id="rId3"/>'
            '</sheets><definedNames/><calcPr calcId="191029"/></workbook>')

WB_RELS = (HDR + '<Relationships>'
           '<Relationship Id="rId1" Type="x/worksheet" Target="worksheets/sheet1.xml"/>'
           '<Relationship Id="rId2" Type="x/worksheet" Target="worksheets/sheet2.xml"/>'
           '<Relationship Id="rId3" Type="x/worksheet" Target="worksheets/sheet3.xml"/>'
           '<Relationship Id="rId4" Type="x/styles" Target="styles.xml"/>'
           '<Relationship Id="rId5" Type="x/sharedStrings" Target="sharedStrings.xml"/>'
           '</Relationships>')

with zipfile.ZipFile(f"{out}/DEMO.XLSX", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", HDR + "<Types/>")
    z.writestr("_rels/.rels", HDR + '<Relationships><Relationship Id="rId1"'
               ' Type="x/officeDocument" Target="xl/workbook.xml"/>'
               '</Relationships>')
    z.writestr("xl/workbook.xml", WORKBOOK)
    z.writestr("xl/_rels/workbook.xml.rels", WB_RELS)
    z.writestr("xl/sharedStrings.xml", SHARED)
    z.writestr("xl/styles.xml", STYLES)
    z.writestr("xl/worksheets/sheet1.xml", SHEET1)
    z.writestr("xl/worksheets/sheet2.xml", SHEET2)
    z.writestr("xl/worksheets/sheet3.xml", SHEET3)

with zipfile.ZipFile(f"{out}/DEMO.XLSX") as z:
    print("DEMO.XLSX")
    for i in z.infolist():
        print(f"  {i.filename:30s} {i.compress_size:6d} -> {i.file_size:6d}")
    print(f"  {len(strings)} shared strings, "
          f"{len(CATEGORIES)} budget lines, 200 inventory lines")
