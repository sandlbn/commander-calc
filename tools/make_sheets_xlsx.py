#!/usr/bin/env python3
"""make_sheets_xlsx — a workbook whose sheets are obviously different.

DEMO.XLSX already has three sheets and is the fixture the importer is
measured against, but its sheets are all dense tables and one looks much
like another on an 80-column screen. This one is for eyeballing sheet
switching: each sheet says its own name in A1, has a different number of
rows, and uses a different column width, so a switch that silently did
nothing would be obvious.

    python3 tools/make_sheets_xlsx.py <dir>      writes SHEETS.XLSX
"""
import sys, zipfile

SHEETS = [
    # name,      rows, width, what each row says
    ("First",     6, 20, "first sheet row"),
    ("Second",   12, 10, "second sheet row"),
    ("Third",     3, 30, "third sheet row"),
    ("Fourth",   20,  8, "fourth"),
    ("Fifth",     1, 14, "fifth sheet has one row"),
]

def sheet_xml(rows, width, text):
    x = ['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
         '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
         '<cols><col min="1" max="1" width="%d" customWidth="1"/></cols>' % width,
         '<sheetData>']
    for r in range(1, rows + 1):
        x.append('<row r="%d">' % r)
        x.append('<c r="A%d" t="inlineStr"><is><t>%s %d</t></is></c>' % (r, text, r))
        x.append('<c r="B%d"><v>%d</v></c>' % (r, r * 100))
        if r == rows and rows > 1:
            # a formula, so each sheet exercises the batch compiler too
            x.append('<c r="C%d"><f>SUM(B1:B%d)</f><v>%d</v></c>'
                     % (r, rows, sum(i * 100 for i in range(1, rows + 1))))
        x.append('</row>')
    x.append('</sheetData></worksheet>')
    return ''.join(x)

def main(outdir):
    path = outdir.rstrip('/') + '/SHEETS.XLSX'
    z = zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED)

    over = ''.join('<Override PartName="/xl/worksheets/sheet%d.xml" ContentType='
                   '"application/vnd.openxmlformats-officedocument.spreadsheetml.'
                   'worksheet+xml"/>' % (i + 1) for i in range(len(SHEETS)))
    z.writestr('[Content_Types].xml',
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
        '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
        '<Default Extension="xml" ContentType="application/xml"/>'
        '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.'
        'openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>' + over + '</Types>')

    z.writestr('_rels/.rels',
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        '<Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/'
        '2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>')

    sheets = ''.join('<sheet name="%s" sheetId="%d" r:id="rId%d"/>'
                     % (n, i + 1, i + 1) for i, (n, _, _, _) in enumerate(SHEETS))
    z.writestr('xl/workbook.xml',
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" '
        'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships">'
        '<sheets>' + sheets + '</sheets></workbook>')

    rels = ''.join('<Relationship Id="rId%d" Type="http://schemas.openxmlformats.org/'
                   'officeDocument/2006/relationships/worksheet" Target="worksheets/'
                   'sheet%d.xml"/>' % (i + 1, i + 1) for i in range(len(SHEETS)))
    z.writestr('xl/_rels/workbook.xml.rels',
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
        + rels + '</Relationships>')

    for i, (name, rows, width, text) in enumerate(SHEETS):
        z.writestr('xl/worksheets/sheet%d.xml' % (i + 1),
                   sheet_xml(rows, width, text))
    z.close()

    print("%s: %d sheets" % (path, len(SHEETS)))
    for name, rows, width, _ in SHEETS:
        print("  %-8s %2d rows, column A %d wide" % (name, rows, width))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "build")
