"""msdiff.py <stock dis.txt | .ko> <ours .o|.ko> <func>: multiset diff of call targets, cmp immediates, constants, field offsets (codegen-blind real-difference filter)"""
import sys, re, subprocess, collections
OD = '<build-dir>'


def listing(path, fn):
    if path.endswith('dis.txt'):
        o = []
        on = False
        for l in open(path, errors='replace').read().split('\n'):
            if re.match(r'[0-9a-f]+ <', l):
                if on:
                    break
                on = l.rstrip().endswith('<%s>:' % fn)
                continue
            if on:
                o.append(l)
        return '\n'.join(o)
    return subprocess.run([OD, '-dr', '--no-show-raw-insn', '--disassemble-symbols=' + fn, path], capture_output=True, text=True).stdout


def feats(path, fn):
    out = listing(path, fn)
    C = collections.Counter()
    seq = []
    for l in out.split('\n'):
        r = re.match(r'\s+[0-9a-f]+:\s+(R_AARCH64_\w+)\s+(\S+)', l)
        if r:
            t = re.sub(r'\+0x[0-9a-f]+$', '', r.group(2))
            if t.startswith('.text.'):
                t = t[6:]
            if 'CALL26' in r.group(1) or 'JUMP26' in r.group(1):
                C['call ' + t] += 1
                seq.append(t)
            continue
        m = re.match(r'\s+[0-9a-f]+:\s+(?:[0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2} [0-9a-f]{2}\s+)?(\S+)\s*(.*)$', l)
        if not m:
            continue
        op, args = m.group(1), m.group(2)
        if op in ('cmp', 'ccmp', 'cmn'):
            mm = re.findall(r'#(-?0x[0-9a-f]+|-?\d+)', args)
            if mm:
                C['cmp #' + mm[0]] += 1
        elif op == 'mov' and '#' in args:
            mm = re.search(r'#(-?0x[0-9a-f]+|-?\d+)', args)
            C['mov #' + mm.group(1)] += 1
        elif op in ('movk', 'movz'):
            C[op + ' ' + args.split(',', 1)[1].strip()] += 1
        elif op in ('ldr', 'str', 'ldrb', 'strb', 'ldur', 'stur', 'ldrsw', 'ldrh', 'strh', 'ldrsb', 'ldrsh'):
            mm = re.search(r'\[(x\d+|sp)(?:, #(-?0x[0-9a-f]+))?\]', args)
            if mm and mm.group(1) not in ('sp', 'x29'):
                C[op.replace('ldur', 'ldr').replace('stur', 'str') + ' [+' + (mm.group(2) or '0') + ']'] += 1
        elif op in ('add', 'sub') and '#' in args and 'sp' not in args:
            mm = re.search(r'#(-?0x[0-9a-f]+|-?\d+)', args)
            C[op + ' #' + mm.group(1)] += 1
    return C, seq


a, sa = feats(sys.argv[1], sys.argv[3])
b, sb = feats(sys.argv[2], sys.argv[3])
print('calls stock', len(sa), 'ours', len(sb))
for k in sorted(set(a) | set(b)):
    if a[k] != b[k]:
        print('%-44s stock %d ours %d' % (k, a[k], b[k]))
if len(sys.argv) > 4:
    print('seq stock:', ' '.join(sa))
    print('seq ours :', ' '.join(sb))
