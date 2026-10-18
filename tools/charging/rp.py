"""rp.py <file.c> <patch.txt>: CRLF-safe multi-replace. patch.txt: blocks '@@@OLD' / old text / '@@@NEW' / new text / '@@@END' (LF in patch, file stays CRLF). Optional '@@@ALL' instead of '@@@OLD' replaces all occurrences."""
import sys
p, pf = sys.argv[1], sys.argv[2]
s = open(p, 'rb').read().decode('latin1')
crlf = '\r\n' in s
t = open(pf, 'rb').read().decode('latin1').replace('\r\n', '\n')
blocks = t.split('@@@')
i = 0
n = 0
while i < len(blocks):
    b = blocks[i]
    if b.startswith('OLD') or b.startswith('ALL'):
        allm = b.startswith('ALL')
        old = b[3:].strip('\n')
        nb = blocks[i + 1]
        assert nb.startswith('NEW'), nb[:20]
        new = nb[3:].strip('\n') if nb[3:].strip('\n') else ''
        if nb[3:] == '\n\n' or nb[3:] == '\n':
            new = ''
        if crlf:
            old = old.replace('\n', '\r\n')
            new = new.replace('\n', '\r\n')
        c = s.count(old)
        if c == 0 or (c > 1 and not allm):
            print('PATCH FAIL (count=%d):' % c, old[:200].replace('\r', ''))
            sys.exit(1)
        s = s.replace(old, new)
        n += 1
        i += 2
    else:
        i += 1
open(p, 'wb').write(s.encode('latin1'))
print('applied', n)
