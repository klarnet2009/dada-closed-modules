"""rep.py FILE START_LINE_TEXT END_LINE_TEXT NEWFILE [N]
Replace the block that begins at the N-th (default 1st) line whose rstripped text == START_LINE_TEXT and ends
at the first following line whose rstripped text == END_LINE_TEXT (inclusive) with the contents of NEWFILE.
CRLF aware: the file keeps CRLF, NEWFILE is LF text. NEWFILE '-' = delete the block. Prints the line range."""
import sys

f, st, en, nf = sys.argv[1:5]
n = int(sys.argv[5]) if len(sys.argv) > 5 else 1
raw = open(f, 'rb').read().decode('latin1')
crlf = '\r\n' in raw
L = raw.split('\n')
if crlf:
    L = [l[:-1] if l.endswith('\r') else l for l in L]
cnt = 0
a = None
for i, l in enumerate(L):
    if l.rstrip() == st.rstrip():
        cnt += 1
        if cnt == n:
            a = i
            break
if a is None:
    sys.exit('start line not found: %r' % st)
b = None
for j in range(a, len(L)):
    if L[j].rstrip() == en.rstrip():
        b = j
        break
if b is None:
    sys.exit('end line not found: %r' % en)
new = [] if nf == '-' else open(nf, 'rb').read().decode('latin1').replace('\r\n', '\n').rstrip('\n').split('\n')
L[a:b + 1] = new
out = ('\r\n' if crlf else '\n').join(L)
open(f, 'wb').write(out.encode('latin1'))
print('replaced lines %d-%d (%d lines) by %d lines' % (a + 1, b + 1, b - a + 1, len(new)))
