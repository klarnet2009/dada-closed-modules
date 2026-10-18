"""repfn.py <file.c> <snippet.c>: replace/insert top-level functions (LF files).
snippet chunks are separated by lines starting with '//@@'.
  '//@@REPL'            : chunk replaces the existing function of the same name (found by its first 'static ... name(' line)
  '//@@NEW <anchor>'    : chunk is inserted before the definition of function <anchor>
  '//@@DEL <name>'      : delete function <name>
Function extent: from the 'static' line (plus directly preceding attribute/comment lines starting with __maybe_unused/__attribute__/noinline) to the first line that is exactly '}'."""
import re, sys
p, sn = sys.argv[1:3]
s = open(p, newline='').read()
t = open(sn, newline='').read().replace('\r\n', '\n')
parts = re.split(r'^//@@(.*)\n', t, flags=re.M)
# parts: [pre, hdr1, body1, hdr2, body2 ...]
def find(name):
    m = re.search(r'^((?:__maybe_unused\n|static noinline\n|__attribute__[^\n]*\n)*)static[^\n;]*?\b' + re.escape(name) + r'\([^;{]*?\)\s*\n?\{.*?^\}\n', s, re.S | re.M)
    return m
for i in range(1, len(parts), 2):
    hdr, body = parts[i].strip(), parts[i + 1].strip('\n') + '\n'
    if hdr.startswith('DEL'):
        name = hdr.split()[1]; m = find(name); assert m, name
        s = s[:m.start()] + s[m.end():].lstrip('\n'); print('deleted', name); continue
    nm = re.search(r'^static[^\n;]*?\b(\w+)\(', body, re.M).group(1)
    if hdr.startswith('REPL'):
        m = find(nm); assert m, 'missing ' + nm
        s = s[:m.start()] + body + s[m.end():]; print('replaced', nm)
    elif hdr.startswith('NEW'):
        anc = hdr.split()[1]; m = find(anc); assert m, 'anchor ' + anc
        assert not find(nm), 'exists ' + nm
        s = s[:m.start()] + body + '\n' + s[m.start():]; print('inserted', nm, 'before', anc)
open(p, 'w', newline='').write(s)
