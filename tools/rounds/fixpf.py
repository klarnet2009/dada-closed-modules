"""fixpf.py <file>: drop the explicit leading "%s(): " + first __func__ arg of pr_xxx(...) calls (pr_fmt already adds them). CRLF/bytes preserved."""
import re, sys
p = sys.argv[1]
s = open(p, encoding='latin1', newline='').read()
pat = re.compile(r'(pr_\w+\()"%s\(\): ((?:[^"\\]|\\.)*)",(\s*)__func__(\s*\)|,\s*)')
n = 0


def f(m):
    global n
    n += 1
    tail = m.group(4)
    if tail.lstrip().startswith(')'):
        return m.group(1) + '"' + m.group(2) + '"' + tail
    return m.group(1) + '"' + m.group(2) + '",' + m.group(3)


s2 = pat.sub(f, s)
print(n)
open(p, 'w', encoding='latin1', newline='').write(s2)
