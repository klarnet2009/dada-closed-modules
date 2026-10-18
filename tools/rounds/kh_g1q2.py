"""kh.py <hexhash> [hexhash...] [-n maxargs] [-t 'extra type']... : brute-force the C signature whose kCFI hash matches.
Compiles every candidate with the kernel flags (pb_cc.sh -S) and reads the `.word` before each function."""
import sys, subprocess, itertools, re, os
args = sys.argv[1:]
nmax = 3; extra = []; want = set()
i = 0
while i < len(args):
    if args[i] == '-n': nmax = int(args[i+1]); i += 2
    elif args[i] == '-t': extra.append(args[i+1]); i += 2
    else: want.add(int(args[i], 16)); i += 1
base = ['void', 'int', 'unsigned int', 'long', 'unsigned long', 'u64', 's64', 'bool', 'u8', 'u16', 'u32', 'char *', 'void *', 'const char *',
        'struct task_struct *', 'struct cpumask *', 'struct ctl_table *', 'size_t *', 'loff_t *', 'size_t', 'struct file *', 'struct inode *', 'struct seq_file *', 'struct work_struct *',
        'struct rq *', 'long long', 'unsigned long long', 'struct irq_work *', 'ssize_t', 'loff_t', 'pid_t', 'uid_t', 'struct cpufreq_policy *', 'struct kobject *', 'struct list_head *']
base += extra
rets = base
argt = [t for t in base if t != 'void']
cands = []
for r in rets:
    cands.append((r, ()))
    for n in range(1, nmax + 1):
        for a in itertools.product(argt, repeat=n):
            cands.append((r, a))
out = []
hdr = '#include <linux/sched.h>\n#include <linux/sysctl.h>\n#include <linux/seq_file.h>\n#include <linux/irq_work.h>\n#include <linux/cpufreq.h>\n#include <linux/kobject.h>\n'
body = []
for k, (r, a) in enumerate(cands):
    pl = ', '.join(('%s a%d' % (t, j)) if not t.endswith('*') else ('%sa%d' % (t, j)) for j, t in enumerate(a)) or 'void'
    body.append('%s f%d(%s) { %s }' % (r, k, pl, '' if r == 'void' else 'return (%s)0;' % r if not r.endswith('*') else 'return 0;'))
open('/tmp/kh_cand.c', 'w').write(hdr + '\n'.join(body) + '\n')
env = dict(os.environ, MODNAME='kh')
p = subprocess.run(['<work-dir>', '/tmp/kh_cand.c', '/tmp/kh_cand.o', '-S', '-O0', '-Wno-error'], env=env, capture_output=True, text=True)
if p.returncode:
    print(p.stdout[-2000:], p.stderr[-2000:]); sys.exit(1)
asm = open('/tmp/kh_cand.o').read()
found = 0
for m in re.finditer(r'\.word\s+(\d+)[^\n]*\n(?:[^\n]*\n){0,3}?f(\d+):', asm):
    h = int(m.group(1)); k = int(m.group(2))
    if h in want:
        r, a = cands[k]
        print('%#x  %s(%s)' % (h, r, ', '.join(a))); found += 1
print('candidates', len(cands), 'found', found)
