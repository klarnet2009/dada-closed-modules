#!/usr/bin/env python3
"""s5ms.py (goal-G25a): per-function multiset of relocation targets (calls and global symbols; section relocs .rodata/.data/.bss are dropped,
.llvm.<hash> suffixes stripped) and of key instructions (acquire/release/atomics/barriers/brk/mrs/prfm; ldapr counted as ldar) for two builds.
Prints only functions that differ.
Usage: python3 -I s5ms.py STOCK.ko OURS.ko [func1,func2,...]      (OBJDUMP=... overrides the llvm-objdump path)
Reading: a CALL symbol that is S-only/O-only is a candidate lost/added call; data-symbol differences are often only `x` vs `.bss+off` naming
(stock keeps local names, ours uses section relocs); 'I:brk' differences are array-bound / WARN trap placement; address ORDER of calls is
meaningless (cold blocks move), counts are what matter."""
import sys, re, subprocess, collections, os

OD = os.environ.get('OBJDUMP', '<build-dir>')
KEY = {'ldar', 'ldarb', 'ldarh', 'stlr', 'stlrb', 'stlrh', 'dmb', 'dsb', 'isb', 'ldaxr', 'ldxr', 'stxr', 'stlxr', 'ldadd', 'stadd', 'swp', 'cas',
       'casal', 'ldset', 'brk', 'mrs', 'msr', 'prfm', 'ldapr', 'ldapur', 'ldclr', 'stclr', 'stset', 'ldeor'}


def parse(ko):
    out = subprocess.run([OD, '-dr', '--no-show-raw-insn', ko], capture_output=True, text=True).stdout
    F = {}
    cur = None
    for l in out.split('\n'):
        m = re.match(r'^[0-9a-f]+ <(.*)>:$', l)
        if m:
            cur = m.group(1)
            F[cur] = collections.Counter()
            continue
        if cur is None:
            continue
        m = re.search(r'R_AARCH64_\w+\s+(\S+)', l)
        if m:
            s = re.sub(r'\+0x[0-9a-f]+$', '', m.group(1))
            s = re.sub(r'\.llvm\.\d+$', '', s)
            if re.match(r'\.(text|rodata|data|bss|init|exit|altinstr|ksymtab)', s):
                if not s.startswith('.text'):
                    continue
                s = '.text'
            F[cur]['R:' + s] += 1
            continue
        m = re.match(r'\s+[0-9a-f]+:\s+(\S+)', l)
        if m and (m.group(1) in KEY or m.group(1).startswith(('ldadd', 'stadd', 'ldset', 'stset', 'ldclr', 'stclr', 'cas', 'swp'))):
            F[cur]['I:' + ('ldar' if m.group(1) == 'ldapr' else m.group(1))] += 1
    return F


S = parse(sys.argv[1])
O = parse(sys.argv[2])
only = set(sys.argv[3].split(',')) if len(sys.argv) > 3 and sys.argv[3] else None
for f in sorted(S):
    if f in O and (not only or f in only):
        ds, do = S[f] - O[f], O[f] - S[f]
        if ds or do:
            print('%s: S-only %s | O-only %s' % (f, dict(ds), dict(do)))
