"""opm.py <mod> <func> [built.ko]: scheduling-agnostic instruction MULTISET diff stock vs built (registers masked, branch targets masked, adrp/adr/add-lo12
dropped, immediates kept). Prints only instructions whose count differs: a semantic difference shows up here, pure reordering/regalloc does not.
Runs on the build server."""
import collections, re, subprocess, sys
B = '<build-dir>'
mod, fn = sys.argv[1:3]
built = sys.argv[3] if len(sys.argv) > 3 else '<work-dir>' % mod


def ops(path):
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', path], text=True)
    out, cur, last = [], False, ''
    for l in txt.split('\n'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = (m.group(1) == fn)
            continue
        if not cur:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(\w+)\s+(\S+)', l)
        if m:
            if m.group(1) in ('CALL26', 'JUMP26'):
                out.append('RELOC-CALL ' + re.sub(r'\.llvm\.\d+$', '', m.group(2)))
            continue
        if re.match(r'^\s+[0-9a-f]+:\s', l):
            ins = l.split(':', 1)[1].strip()
            if '.word' in ins:
                continue
            ins = re.sub(r'<[^>]*>', '', ins)
            ins = re.sub(r'\s*//.*$', '', ins)
            if re.match(r'^(adrp|adr)\s', ins) or re.match(r'^add\s+\w+, \w+, #0x0$', ins):
                continue
            if re.match(r'^bl\s', ins):
                continue
            ins = re.sub(r'^(b\.?\w*|cbn?z|tbn?z)(\s.*?)0x[0-9a-f]+\s*$', r'\1\2L', ins)
            ins = re.sub(r'\b[xw](\d+|zr)\b', 'R', ins)
            ins = re.sub(r'\bsp\b', 'SP', ins)
            out.append(re.sub(r'\s+', ' ', ins))
    return out


a = collections.Counter(ops('<work-dir>' % mod))
b = collections.Counter(ops(built))
print('stock %d built %d' % (sum(a.values()), sum(b.values())))
for k in sorted(set(a) | set(b)):
    if a[k] != b[k]:
        print('%+d  %s' % (b[k] - a[k], k))
