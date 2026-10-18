"""st.py <module> <textfile> : insert the text after the first line of work/re/src/<module>/STATUS.md (keeps CRLF/LF)"""
import sys, os
m, t = sys.argv[1:3]
p = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', m, 'STATUS.md')
s = open(p, newline='', encoding='utf8').read()
nl = '\r\n' if '\r\n' in s else '\n'
i = s.index(nl) + len(nl)
add = open(t, encoding='utf8').read().replace('\r\n', '\n').replace('\n', nl)
open(p, 'w', newline='', encoding='utf8').write(s[:i] + add + s[i:])
