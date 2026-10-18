"""st_ins.py <STATUS.md> <newfirstline-file> <section-file>: replace first line, insert section after it (keeps CRLF style)."""
import sys
p, fl, sec = sys.argv[1:4]
raw = open(p, 'rb').read().decode('utf-8')
crlf = '\r\n' in raw
t = raw.replace('\r\n', '\n')
first = open(fl, encoding='utf-8').read().strip('\n')
add = open(sec, encoding='utf-8').read().strip('\n')
rest = t.split('\n', 1)[1]
t = first + '\n\n' + add + '\n' + rest
if crlf:
    t = t.replace('\n', '\r\n')
open(p, 'wb').write(t.encode('utf-8'))
print('ok', p)
