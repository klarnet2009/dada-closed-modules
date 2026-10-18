"""applypins.py <source.c> <pinmap.txt>: rewrite '#line OLD' pins (OLD -> NEW) per function from pinmap output (func old new); stock-line = NEW"""
import re, sys
src = open(sys.argv[1], newline="").read().replace(chr(13) + chr(10), chr(10))
pairs = [l.split() for l in open(sys.argv[2]).read().split("\n") if l and not l.startswith("#")]
done = 0; miss = []
for fn, old, new in pairs:
    # function body region
    m = re.search(r"^[^\n;]*\b%s\(.*?\n\{\n.*?\n\}\n" % re.escape(fn), src, re.S | re.M)
    pat = "#line %s\n" % old
    repl = "#line @@%s@@\n" % new
    if m and pat in m.group(0):
        body = m.group(0).replace(pat, repl, 1)
        src = src[:m.start()] + body + src[m.end():]; done += 1
    elif pat in src:
        src = src.replace(pat, repl, 1); done += 1
        miss.append((fn, old, new, "outside"))
    else:
        miss.append((fn, old, new, "notfound"))
src = re.sub(r"#line @@(\d+)@@", r"#line \1", src)
open(sys.argv[1], "w", newline="").write(src)
print("applied", done, "problems", miss)
