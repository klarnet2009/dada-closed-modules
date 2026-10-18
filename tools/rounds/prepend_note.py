"""prepend_note.py STATUS.md note.md [newfirstline]: insert note.md after the first line of STATUS.md (keeps CRLF); optional new first line"""
import sys
p, n = sys.argv[1], sys.argv[2]
s = open(p, 'rb').read().decode()
nl = '\r\n' if '\r\n' in s else '\n'
lines = s.replace('\r\n', '\n').split('\n')
note = open(n, 'rb').read().decode().replace('\r\n', '\n').rstrip('\n').split('\n')
first = sys.argv[3] if len(sys.argv) > 3 else lines[0]
open(p, 'wb').write(nl.join([first, ''] + note + [''] + lines[1:]).encode())
