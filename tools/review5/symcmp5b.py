#!/usr/bin/env python3
"""symcmp5b.py (goal-G25a): wrapper around symcmp5.py (which stays unchanged).
Fixes hit during the all-modules audit:
  * `symcmp5.py stock.ko not_an_elf.ko ...` died with a traceback inside compare_ko_v2 (a 130-byte placeholder ended up as 'ours'): now a clear error message and a non-zero exit.
  * function lists: `@file` arguments are expanded (one name per line, or `=== name:` lines of a symlm/report file), so 133 names need no shell juggling.
  * S5TMO defaults to 150 s per function when unset; `--summary` prints a verdict count table after the run.
Usage: python3 -I symcmp5b.py STOCK.ko OURS.ko (all | name... | @list.txt) [-v] [--summary]
Known limit NOT fixed here (see README): ret(x) vs ret(0) when the path implies x==0 (cbz/cmp #0 before the return) is reported as POLARITY-DIFF [reach] ret(...)."""
import sys, os, re, io, runpy, collections

here = os.path.dirname(os.path.abspath(__file__))
a = sys.argv[1:]
summary = '--summary' in a
a = [x for x in a if x != '--summary']


def is_elf(p):
    try:
        return open(p, 'rb').read(4) == b'\x7fELF'
    except OSError:
        return False


if len(a) < 3 or not (is_elf(a[0]) and is_elf(a[1])):
    sys.exit('usage: symcmp5b.py STOCK.ko OURS.ko (all|names|@list) ; both inputs must be ELF files (got %r)' % (a[:2],))
out = a[:2]
for x in a[2:]:
    if x.startswith('@'):
        for l in open(x[1:], encoding='utf-8', errors='replace'):
            m = re.match(r'^(?:===\s*)?([A-Za-z_][A-Za-z0-9_.]*)\b', l.strip())
            if m:
                out.append(m.group(1))
    else:
        out.append(x)
os.environ.setdefault('S5TMO', '150')
sys.argv = [os.path.join(here, 'symcmp5.py')] + out
buf = io.StringIO()
if summary:
    class Tee:
        def write(self, s):
            sys.__stdout__.write(s)
            buf.write(s)

        def flush(self):
            sys.__stdout__.flush()
    sys.stdout = Tee()
try:
    runpy.run_path(sys.argv[0], run_name='__main__')
finally:
    if summary:
        c = collections.Counter(''.join(t) for t in re.findall(r'=> ([A-Z-]+)|(INCONCLUSIVE)|(ERROR \w+)', buf.getvalue()))
        sys.__stdout__.write('\nVERDICT COUNTS: %s\n' % dict(c))
