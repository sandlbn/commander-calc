#!/usr/bin/env python3
"""Build .xlsx fixtures for the importer tests.

Written with python's zipfile rather than a spreadsheet, so the parts are
exactly the shapes Excel and LibreOffice emit — including the ones that
differ between them, which is where importers break.
"""
import sys, zipfile

out = sys.argv[1] if len(sys.argv) > 1 else "build/host/sd"

RELS = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n'

def book(sheets, extra=""):
    s = "".join(
        f'<sheet name="{n}" sheetId="{i+1}" r:id="rId{i+1}"{st}/>'
        for i, (n, st) in enumerate(sheets))
    return (RELS + '<workbook xmlns:r="http://schemas.openxmlformats.org/'
            'officeDocument/2006/relationships">'
            f'<fileVersion appName="xl"/><sheets>{s}</sheets>{extra}'
            '</workbook>')

def rels(n, extra=""):
    r = "".join(
        f'<Relationship Id="rId{i+1}" Type="http://schemas.openxmlformats.org'
        f'/officeDocument/2006/relationships/worksheet" '
        f'Target="worksheets/sheet{i+1}.xml"/>' for i in range(n))
    return RELS + f'<Relationships>{r}{extra}</Relationships>'

def shared(items):
    body = "".join(items)
    return (RELS + f'<sst count="{len(items)}" uniqueCount="{len(items)}">'
            f'{body}</sst>')

def sheet(rows=""):
    return RELS + f'<worksheet><sheetData>{rows}</sheetData></worksheet>'

# --- a normal three-sheet workbook, the way Excel writes one -----------
with zipfile.ZipFile(f"{out}/BOOK.XLSX", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("[Content_Types].xml", RELS + "<Types/>")
    z.writestr("_rels/.rels", RELS + '<Relationships><Relationship Id="rId1"'
               ' Type="http://schemas.openxmlformats.org/officeDocument/2006'
               '/relationships/officeDocument" Target="xl/workbook.xml"/>'
               '</Relationships>')
    z.writestr("xl/workbook.xml",
               book([("Budget", ""), ("Inventory", ""), ("Summary", "")],
                    '<definedNames/><calcPr calcId="1"/>'))
    z.writestr("xl/_rels/workbook.xml.rels",
               rels(3, '<Relationship Id="rId4" Type="http://schemas.'
                       'openxmlformats.org/officeDocument/2006/relationships'
                       '/sharedStrings" Target="sharedStrings.xml"/>'))
    z.writestr("xl/sharedStrings.xml", shared([
        "<si><t>Name</t></si>",
        "<si><t>Quantity</t></si>",
        # rich text: several runs, which must concatenate
        "<si><r><rPr><b/></rPr><t>Hello</t></r><r><t> world</t></r></si>",
        # the entity and unicode paths
        "<si><t>Smith &amp; Co</t></si>",
        "<si><t>café €5</t></si>",
        # whitespace that Excel marks as significant
        '<si><t xml:space="preserve"> padded </t></si>',
    ]))
    for i in range(1, 4):
        z.writestr(f"xl/worksheets/sheet{i}.xml", sheet())

# --- a hidden sheet, and a target spelled absolutely ------------------
with zipfile.ZipFile(f"{out}/ODD.XLSX", "w", zipfile.ZIP_DEFLATED) as z:
    z.writestr("xl/workbook.xml",
               book([("Visible", ""), ("Gone", ' state="hidden"')]))
    z.writestr("xl/_rels/workbook.xml.rels", RELS + '<Relationships>'
               '<Relationship Id="rId1" Type="x/worksheet" '
               'Target="/xl/worksheets/sheet1.xml"/>'
               '<Relationship Id="rId2" Type="x/worksheet" '
               'Target="worksheets/sheet2.xml"/></Relationships>')
    z.writestr("xl/sharedStrings.xml", shared([]))

print("wrote BOOK.XLSX and ODD.XLSX to", out)
