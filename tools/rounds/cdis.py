"""Compact view of a dis.txt (llvm-objdump -dr): merges reloc lines into the instruction, drops raw noise.
usage: cdis.py <dis.txt> [func ...]   (no func: list function names + sizes)
"""
import re, sys

def parse(path):
    funcs = {}; order = []; cur = None; sec = ''
    for line in open(path, errors='replace'):
        line = line.rstrip('\n')
        m = re.match(r'^Disassembly of section (\S+):', line)
        if m: sec = m.group(1); continue
        m = re.match(r'^([0-9a-f]{16}) <([^>]+)>:$', line)
        if m:
            cur = m.group(2)
            if cur.startswith('.'): cur = sec + cur if cur in ('.text',) else cur
            funcs[cur] = []; order.append(cur); continue
        if cur is None: continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(R_AARCH64_\w+)\s+(.*)$', line)
        if m:
            if funcs[cur]:
                o, ins = funcs[cur][-1]
                rt = m.group(2).replace('R_AARCH64_', '')
                funcs[cur][-1] = (o, ins + '  ;' + rt + ' ' + m.group(3).strip())
            continue
        m = re.match(r'^\s+([0-9a-f]+):\s+(?:[0-9a-f]{2} ){4}\s*(.*)$', line)
        if m:
            funcs[cur].append((int(m.group(1), 16), m.group(2).strip())); continue
        m = re.match(r'^\s+([0-9a-f]+):\s*(.*)$', line)
        if m:
            funcs[cur].append((int(m.group(1), 16), m.group(2).strip()))
    return funcs, order

if __name__ == '__main__':
    funcs, order = parse(sys.argv[1])
    want = sys.argv[2:]
    if not want:
        for n in order: print(n, len(funcs[n]))
    for n in want:
        print('=====', n)
        for o, ins in funcs.get(n, []):
            ins = re.sub(r'0x[0-9a-f]+ <([^>]*)>', lambda m: '<' + m.group(1).split('+')[-1] + '>' if '+' in m.group(1) else '<' + m.group(1) + '>', ins)
            ins = re.sub(r'\s+', ' ', ins)
            print('%x: %s' % (o, ins))
