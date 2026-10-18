"""linehack.py - __LINE__ alignment HACK (documented in STATUS.md, ).
The OS4 stock embeds source line numbers (mov wN,#line in qdf_trace/_fl helpers).  Where a function is source-identical to the stock but the file has
a different number of lines before it (newer CAF patches, Xiaomi comments ...), only those immediates differ.  This tool shifts __LINE__ inside such
functions with   #line <orig+d>  before the opening brace and  #line <orig next line>  after the closing brace (all other lines keep their numbers).
  linehack.py gen <diff.cls> <out.json> <pkgdir>   : from the offpairs2 classification (IMM functions with ONE immediate delta d) find the defining file
  linehack.py apply <json> <pkgdir>                : strip old hack lines, insert the new ones (idempotent: lines carry the marker /*g4l-lh*/)
  linehack.py strip <pkgdir>                       : remove all hack lines"""
import json, os, re, sys

MARK = "/*g4l-lh*/"


def files(pkg):
    for root, _, fs in os.walk(pkg):
        for f in fs:
            if f.endswith(".c"):
                yield os.path.join(root, f)


def strip_text(t):
    return "".join(l for l in t.splitlines(True) if MARK not in l)


def find_def(lines, name):
    """index of the line holding the opening '{' (col 0) of the definition of name, or None. Definition: line start (optional type words) name( ... then '{' at col 0 within 25 lines, no ';' ended statement before."""
    pat = re.compile(r"^(?:[A-Za-z_][\w \t\*]*[ \t\*])?%s\(" % re.escape(name))
    hits = []
    for i, l in enumerate(lines):
        if pat.match(l) and not l.rstrip().endswith(";"):
            for j in range(i, min(i + 25, len(lines))):
                if lines[j].startswith("{"):
                    hits.append(j)
                    break
                if lines[j].rstrip().endswith(";") and j > i:
                    break
    return hits[0] if len(hits) == 1 else None


def end_of_func(lines, j):
    for k in range(j + 1, len(lines)):
        if lines[k].startswith("}"):
            return k
    return None


def gen(cls, out, pkg):
    want = {}
    for l in open(cls):
        p = l.split(None, 4)
        if len(p) < 5 or p[1] != "IMM":
            continue
        ds = re.findall(r"([+-]\d+):\d+", p[4])
        if len(ds) == 1:
            want[p[0]] = int(ds[0])
    res = {}
    names = set(want)
    cnt = {}
    # one pass over the files: identifiers that start a definition
    for f in files(pkg):
        try:
            t = open(f, errors="replace", newline="").read()
        except OSError:
            continue
        found = set(re.findall(r"^(?:[A-Za-z_][\w \t\*]*[ \t\*])?(\w+)\(", t, re.M)) & names
        if not found:
            continue
        lines = t.splitlines(True)
        for n in found:
            j = find_def(lines, n)
            if j is not None:
                cnt.setdefault(n, []).append(os.path.relpath(f, pkg))
    for n, fl in cnt.items():
        if len(fl) == 1:
            res.setdefault(fl[0], {})[n] = want[n]
    json.dump(res, open(out, "w"), indent=1)
    print("functions wanted %d, located uniquely %d in %d files" % (len(want), sum(len(v) for v in res.values()), len(res)))


def apply(js, pkg):
    res = json.load(open(js))
    nf = nn = 0
    for rel, fn in res.items():
        p = os.path.join(pkg, rel)
        t = strip_text(open(p, errors="replace", newline="").read())
        lines = t.splitlines(True)
        ins = []  # (index, text) insert before index
        for n, d in fn.items():
            j = find_def(lines, n)
            if j is None:
                continue
            k = end_of_func(lines, j)
            if k is None:
                continue
            ins.append((j, "#line %d %s\n" % (j + 1 + d, MARK)))  # the '{' line itself is physical line j+1 (1-based)
            ins.append((k + 1, "#line %d %s\n" % (k + 2, MARK)))  # first line after the closing brace
            nn += 1
        for idx, text in sorted(ins, key=lambda x: -x[0]):
            lines.insert(idx, text)
        open(p, "w", newline="").write("".join(lines))
        nf += 1
    print("applied %d functions in %d files" % (nn, nf))


def strip(pkg):
    n = 0
    for f in files(pkg):
        t = open(f, errors="replace", newline="").read()
        if MARK in t:
            open(f, "w", newline="").write(strip_text(t))
            n += 1
    print("stripped %d files" % n)


if __name__ == "__main__":
    m = sys.argv[1]
    if m == "gen":
        gen(sys.argv[2], sys.argv[3], sys.argv[4])
    elif m == "apply":
        apply(sys.argv[2], sys.argv[3])
    else:
        strip(sys.argv[2])
