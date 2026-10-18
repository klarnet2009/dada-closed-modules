"""rmfunc.py <file> <name>...: delete the definitions (not prototypes) of the named static functions (brace matching at column 0)."""
import re, sys
p = sys.argv[1]
s = open(p, newline='').read().split('\n')
for name in sys.argv[2:]:
    i = 0; removed = 0
    while i < len(s):
        l = s[i]
        if re.match(r"static [^;(]*\b%s\(" % re.escape(name), l) and not l.rstrip().endswith(';'):
            # find opening brace line then the closing '}' at col 0
            j = i
            while j < len(s) and not s[j].startswith('{'): j += 1
            k = j
            while k < len(s) and s[k] != '}': k += 1
            del s[i:k + 1]
            if i < len(s) and s[i] == '': del s[i]
            removed += 1
            continue
        i += 1
    print(name, 'removed', removed)
open(p, 'w', newline='').write('\n'.join(s))
