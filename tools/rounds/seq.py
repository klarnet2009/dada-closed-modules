"""seq.py <stock.ko> <built.ko> func...: compare the call sequence with (w0..w3 immediates known at the call): linear listing order of the code, difflib.
Value tracking: mov wN/xN,#imm, movk, mov wN,wM. Branch targets ignored; prints only differing hunks of the call sequence."""
import difflib, re, subprocess, sys
B = '<build-dir>'


def funcs(path):
    txt = subprocess.check_output([B, '-dr', '--no-show-raw-insn', path], text=True, errors='replace').split(chr(10))
    res, cur = {}, None
    for l in txt:
        m = re.match(r'^[0-9a-f]{16} <([^>]+)>:$', l)
        if m:
            cur = re.sub(r'\.llvm\.\d+$', '', m.group(1)); res[cur] = []; continue
        if cur is None:
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+R_AARCH64_(CALL26|JUMP26)\s+(\S+)', l)
        if m and res[cur]:
            res[cur][-1] = (res[cur][-1][0] + ' CALL ' + re.sub(r'\.llvm\.\d+$', '', m.group(2)).replace('.text.', ''),)
            continue
        m = re.match(r'^\s+[0-9a-f]+:\s+(.*)$', l)
        if m and not l.strip().startswith('R_'):
            res[cur].append((re.sub(r'\s+', ' ', m.group(1).split('//')[0]).strip(),))
    return res


def seq(ins):
    regs, out = {}, []
    for (t,) in ins:
        m = re.match(r'^mov ([wx])(\d) , ?#(-?0x[0-9a-f]+|-?\d+)'.replace(' ,', ','), t)
        m = re.match(r'^mov ([wx])(\d+), #(-?0x[0-9a-f]+|-?\d+)', t)
        if m:
            v = int(m.group(3), 0)
            regs[int(m.group(2))] = v & 0xffffffff; continue
        m = re.match(r'^movk [wx](\d+), #(0x[0-9a-f]+)(?:, lsl #(\d+))?', t)
        if m:
            r = int(m.group(1)); sh = int(m.group(3) or 0)
            regs[r] = (regs.get(r, 0) & ~(0xffff << sh)) | (int(m.group(2), 16) << sh); continue
        m = re.match(r'^mov [wx](\d+), wzr', t)
        if m:
            regs[int(m.group(1))] = 0; continue
        m = re.match(r'^mov ([wx])(\d+), ([wx])(\d+)$', t)
        if m:
            regs[int(m.group(2))] = regs.get(int(m.group(4)), None); continue
        if 'CALL' in t and t.startswith(('bl ', 'b ')):
            tgt = t.split('CALL ')[1]
            if tgt.startswith(('tfa_reg_write', 'tfa_set_bf', 'tfa_reg_read', 'tfa_wait4manstate', 'tfa_irq_init', 'tfa_mem', 'tfa98xx_dsp', 'tfa_dev', 'tfa_dsp')) or True:
                a = ','.join('-' if regs.get(i) is None else hex(regs[i]) for i in (1, 2, 3))
                out.append('%s(%s)' % (tgt, a))
            regs = {k: v for k, v in regs.items() if k >= 19}
    return out


A, Bn = funcs(sys.argv[1]), funcs(sys.argv[2])
for f in sys.argv[3:]:
    x, y = seq(A.get(f, [])), seq(Bn.get(f, []))
    if x == y:
        print('=== %s: call sequence IDENTICAL (%d calls)' % (f, len(x)))
        continue
    print('=== %s: stock %d calls, built %d calls' % (f, len(x), len(y)))
    sm = difflib.SequenceMatcher(None, x, y, autojunk=False)
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        print('  @@ %d/%d' % (i1, j1))
        for s in x[i1:i2][:25]:
            print('  -', s)
        for s in y[j1:j2][:25]:
            print('  +', s)
