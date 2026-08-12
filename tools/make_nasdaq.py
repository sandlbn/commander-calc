#!/usr/bin/env python3
"""make_nasdaq — a real price history, at whatever size you ask for.

    python3 tools/make_nasdaq.py <dir> [rows] [name]

NASDAQ Composite (^IXIC) daily opens, highs, lows, closes and volumes,
fetched from Yahoo's chart API in five-year windows because the `max` range
is served monthly. Real numbers, not invented ones: a capacity test wants
the awkward value distribution that real data has -- prices from 200 to
20000, volumes in the billions, and gaps where the market was shut.

NOT wired into `make examples`, because that would put a network fetch in
the build. Run it when you want the file; it caches the download in
build/ndq_daily.json and re-uses it.

Sizes are the point. The interesting limits are not the 65535 rows and 256
columns of the address space:

  - 512 formulas per import (FPEND_MAX). Past that the importer sets
    `truncated` and the rest arrive as the values Excel cached. So the
    Prices sheet is deliberately all data and the formulas live on a
    Summary sheet, one row a year -- which is what a real price history
    looks like anyway.
  - Banked RAM, at 8 bytes a cell plus per-row overhead. 512 KB on the
    stock machine, 2 MB in the emulator with -ram 2048.
  - The worksheet XML has to be staged in banked RAM and inflated there
    before it can be parsed, so the FILE is a limit as well as the sheet.

Volumes are rounded to thousands. A 5-byte MBF number carries about 24 bits
of mantissa, so 7,443,220,000 would land on a neighbouring value and look
like a transcription error; in thousands it is exact and the column still
says what it means.
"""
import sys, os, json, time, datetime, urllib.request

CACHE = "ndq_daily.json"
EPOCH = datetime.date(1899, 12, 30)      # what a spreadsheet serial counts from


def fetch(cache_dir):
    path = os.path.join(cache_dir, CACHE)
    if os.path.exists(path):
        return {int(k): v for k, v in json.load(open(path)).items()}

    out = {}
    for start in range(1985, 2027, 5):
        end = min(start + 5, 2027)
        p1 = int(time.mktime(datetime.date(start, 1, 1).timetuple()))
        p2 = int(time.mktime(datetime.date(end, 1, 1).timetuple()))
        url = ("https://query1.finance.yahoo.com/v8/finance/chart/%5EIXIC"
               "?period1={}&period2={}&interval=1d".format(p1, p2))
        req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
        d = json.load(urllib.request.urlopen(req, timeout=45))["chart"]["result"][0]
        q = d["indicators"]["quote"][0]
        for i, t in enumerate(d["timestamp"]):
            if q["close"][i] is not None:
                out[t] = [q["open"][i], q["high"][i], q["low"][i],
                          q["close"][i], q["volume"][i]]
        print("  fetched {}-{} ({} rows so far)".format(start, end, len(out)))
    json.dump({str(k): v for k, v in sorted(out.items())}, open(path, "w"))
    return out


def build(rows, data):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from make_examples import Sheet

    keys = sorted(data)[-rows:]          # the most recent `rows` days

    p = Sheet("Prices", [12, 11, 11, 11, 11, 13])
    p.text("A1", "Date");  p.text("B1", "Open"); p.text("C1", "High")
    p.text("D1", "Low");   p.text("E1", "Close"); p.text("F1", "Vol 000s")
    for i, t in enumerate(keys):
        o, h, l, c, v = data[t]
        d = datetime.date.fromtimestamp(t)
        r = i + 2
        p.num("A%d" % r, (d - EPOCH).days, 3)          # date serial
        p.num("B%d" % r, round(o, 2), 4)
        p.num("C%d" % r, round(h, 2), 4)
        p.num("D%d" % r, round(l, 2), 4)
        p.num("E%d" % r, round(c, 2), 4)
        p.num("F%d" % r, int((v or 0) / 1000), 5)

    # One row a year, so the formula count stays well inside FPEND_MAX
    # however many days the Prices sheet holds.
    years = {}
    for i, t in enumerate(keys):
        years.setdefault(datetime.date.fromtimestamp(t).year,
                         []).append(i + 2)

    s = Sheet("Summary", [8, 12, 12, 12, 12, 10])
    s.text("A1", "Year"); s.text("B1", "Open"); s.text("C1", "High")
    s.text("D1", "Low");  s.text("E1", "Close"); s.text("F1", "Days")
    r = 2
    for y in sorted(years):
        lo, hi = years[y][0], years[y][-1]
        vals = [data[keys[i - 2]] for i in years[y]]
        s.num("A%d" % r, y)
        s.formula("B%d" % r, "Prices!B%d" % lo, round(vals[0][0], 2), 4)
        s.formula("C%d" % r, "MAX(Prices!C%d:C%d)" % (lo, hi),
                  round(max(v[1] for v in vals), 2), 4)
        s.formula("D%d" % r, "MIN(Prices!D%d:D%d)" % (lo, hi),
                  round(min(v[2] for v in vals), 2), 4)
        s.formula("E%d" % r, "Prices!E%d" % hi, round(vals[-1][3], 2), 4)
        s.formula("F%d" % r, "COUNT(Prices!E%d:E%d)" % (lo, hi), len(vals), 5)
        r += 1
    s.text("A%d" % r, "All")
    s.formula("C%d" % r, "MAX(C2:C%d)" % (r - 1),
              round(max(v[1] for v in (data[k] for k in keys)), 2), 4)
    s.formula("D%d" % r, "MIN(D2:D%d)" % (r - 1),
              round(min(v[2] for v in (data[k] for k in keys)), 2), 4)
    s.formula("F%d" % r, "SUM(F2:F%d)" % (r - 1), len(keys), 5)

    formulas = sum(1 for sh in (p, s) for row in sh.rows.values()
                   for cell in row.values() if cell[1] == "f")
    cells = sum(len(row) for sh in (p, s) for row in sh.rows.values())
    return [p, s], cells, formulas


def main(outdir, rows, name, compress=False):
    from make_examples import write
    data = fetch(outdir)
    sheets, cells, formulas = build(min(rows, len(data)), data)
    path = os.path.join(outdir, name)
    write(path, sheets, compress)
    print("%-14s %6d cells, %3d formulas, %d bytes on disk"
          % (name, cells, formulas, os.path.getsize(path)))
    if formulas > 512:
        print("  WARNING: over FPEND_MAX; the importer will truncate")


if __name__ == "__main__":
    d = sys.argv[1] if len(sys.argv) > 1 else "build"
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    nm = sys.argv[3] if len(sys.argv) > 3 else "NASDAQ.XLSX"
    # Uncompressed by default: inflating is 54% of the time it takes to
    # open a workbook, and the reader skips it entirely for a stored entry.
    # Pass "deflate" as a fourth argument for a small file instead of a
    # fast one.
    main(d, n, nm, len(sys.argv) > 4 and sys.argv[4] == "deflate")
