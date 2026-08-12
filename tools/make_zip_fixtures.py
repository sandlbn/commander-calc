#!/usr/bin/env python3
"""Build the ZIP fixtures the reader is tested against.

Deliberately uses python's zipfile rather than anything we wrote: the point
is to read archives produced by a normal writer, the way a real .xlsx is.
"""
import sys, zipfile

out = sys.argv[1] if len(sys.argv) > 1 else "build/host/sd"

# A miniature .xlsx: the entries a real one has, in the order Excel writes
# them, with both storage methods represented.
with zipfile.ZipFile(f"{out}/TEST.ZIP", "w") as z:
    z.writestr("[Content_Types].xml",
               '<?xml version="1.0"?><Types/>', zipfile.ZIP_STORED)
    z.writestr("_rels/.rels", '<?xml version="1.0"?><Relationships/>',
               zipfile.ZIP_DEFLATED)
    z.writestr("xl/workbook.xml",
               '<?xml version="1.0"?><workbook><sheets>'
               '<sheet name="Budget" sheetId="1" r:id="rId1"/>'
               '</sheets></workbook>', zipfile.ZIP_DEFLATED)
    # Long and repetitive, so DEFLATE actually compresses it.
    z.writestr("xl/worksheets/sheet1.xml",
               "<row>" + ("<c r='A1'><v>1</v></c>" * 200) + "</row>",
               zipfile.ZIP_DEFLATED)
    z.writestr("xl/sharedStrings.xml", "shared strings go here",
               zipfile.ZIP_STORED)

with zipfile.ZipFile(f"{out}/EMPTY.ZIP", "w"):
    pass

print("wrote TEST.ZIP and EMPTY.ZIP to", out)
