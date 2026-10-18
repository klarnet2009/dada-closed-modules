"""opd2.py <mod> <func>: instruction diff stock vs built with REGISTERS masked but immediates/conditions kept, branch targets masked,
adrp/add merged noise dropped (runs on the build server; env CTX = context lines)."""
import re, subprocess, sys, difflib, os
B = '<build-dir>'
mod, fn = sys.argv[1:3]
CTX = int(os.environ.get('CTX', '2'))


def ops(path):
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', path], text=True)
    out, cur = [], False
    for l in txt.split('\n'):
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = (m.group(1) == fn)
            continue
        if cur and re.match(r'^\s+[0-9a-f]+:\s', l) and 'R_AARCH64' not in l:
            ins = l.split(':', 1)[1].strip()
            if '.word' in ins:
                continue
            ins = re.sub(r'<[^>]*>', '', ins)
            ins = re.sub(r'\s*//.*$', '', ins)
            ins = re.sub(r'^(b\.?\w*|cbn?z|tbn?z)(\s.*?)0x[0-9a-f]+\s*$', r'\1\2L', ins)
            ins = re.sub(r'\b[xw](\d+|zr)\b', 'R', ins)
            ins = re.sub(r'\bsp\b', 'SP', ins)
            out.append(re.sub(r'\s+', ' ', ins))
    return out


a = ops('<work-dir>' % mod)
b = ops('<work-dir>' % mod)
print(len(a), len(b))
for l in difflib.unified_diff(a, b, lineterm='', n=CTX):
    print(l)
