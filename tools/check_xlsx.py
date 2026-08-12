#!/usr/bin/env python3
"""check_xlsx — is what x16sheet wrote actually an .xlsx?

The host test round-trips exported files through our own importer, which
proves we agree with ourselves. This checks the things only an outside
reader can: that the ZIP structure is valid to a library that did not write
it, that every part is well-formed XML, and that the parts an .xlsx is
required to have are present and point at each other.

    make checkxlsx                  # the file the test suite wrote
    python3 tools/check_xlsx.py F   # any other
"""
import sys, zipfile, xml.etree.ElementTree as ET

REQUIRED = [
    "[Content_Types].xml",
    "_rels/.rels",
    "xl/workbook.xml",
    "xl/_rels/workbook.xml.rels",
    "xl/worksheets/sheet1.xml",
]

def main(path):
    bad = 0
    try:
        z = zipfile.ZipFile(path)
    except zipfile.BadZipFile as e:
        print(f"  not a zip: {e}")
        return 1

    # testzip() verifies every entry's CRC against its data.
    broken = z.testzip()
    if broken:
        print(f"  bad CRC in {broken}")
        bad += 1

    names = z.namelist()
    print(f"  {len(names)} entries, {sum(i.file_size for i in z.infolist())} bytes uncompressed")
    for n in names:
        i = z.getinfo(n)
        method = "stored" if i.compress_type == 0 else f"method {i.compress_type}"
        print(f"    {n:<34} {i.file_size:>8}  {method}")

    for r in REQUIRED:
        if r not in names:
            print(f"  MISSING required part: {r}")
            bad += 1

    for n in names:
        if not n.endswith((".xml", ".rels")):
            continue
        try:
            ET.fromstring(z.read(n))
        except ET.ParseError as e:
            print(f"  MALFORMED {n}: {e}")
            bad += 1

    # The sheet the workbook's relationship actually names must exist.
    try:
        rels = ET.fromstring(z.read("xl/_rels/workbook.xml.rels"))
        ns = "{http://schemas.openxmlformats.org/package/2006/relationships}"
        targets = {r.get("Id"): r.get("Target") for r in rels.iter(f"{ns}Relationship")}
        wb = ET.fromstring(z.read("xl/workbook.xml"))
        rns = "{http://schemas.openxmlformats.org/officeDocument/2006/relationships}"
        for sh in wb.iter():
            if sh.tag.endswith("}sheet"):
                rid = sh.get(f"{rns}id")
                tgt = targets.get(rid)
                if tgt is None:
                    print(f"  sheet {sh.get('name')!r} has no relationship {rid}")
                    bad += 1
                elif f"xl/{tgt}" not in names:
                    print(f"  sheet {sh.get('name')!r} points at missing xl/{tgt}")
                    bad += 1
                else:
                    print(f"  sheet {sh.get('name')!r} -> xl/{tgt}  ok")
    except Exception as e:
        print(f"  relationship check failed: {e}")
        bad += 1

    print("  OK" if bad == 0 else f"  {bad} problem(s)")
    return 1 if bad else 0

if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "build/host/sd/OUT.XLSX"))
