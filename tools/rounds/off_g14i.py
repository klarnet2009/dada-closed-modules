"""off.py 'struct task_struct' field... : print offsetof() of fields of a kernel struct (runs on the server, compiles with kernel flags).
usage on server: python3 <work-dir> 'struct task_struct' pid comm ..."""
import sys, subprocess, re, os
st = sys.argv[1]
fl = sys.argv[2:]
hdr = '#include <linux/sched.h>\n#include <linux/sched/signal.h>\n#include <linux/cpufreq.h>\n#include <linux/fs.h>\n#include <linux/cred.h>\n#include <linux/seq_file.h>\n'
body = ''.join('int o%d(void){return offsetof(%s,%s);}\n' % (i, st, f) for i, f in enumerate(fl))
body += 'int osz(void){return sizeof(%s);}\n' % st
open('/tmp/off.c', 'w').write(hdr + body)
subprocess.run(['<work-dir>', '/tmp/off.c', '/tmp/off.s', '-S', '-O1'], env=dict(os.environ, MODNAME='x'), capture_output=True)
a = open('/tmp/off.s').read()
for i, f in enumerate(fl):
    m = re.search(r'\no%d:[^\n]*\n(?:[^\n]*\n){0,12}?\s+mov\s+w0, #(\d+)' % i, a)
    print(f, hex(int(m.group(1))) if m else '?')
m = re.search(r'\nosz:[^\n]*\n(?:[^\n]*\n){0,12}?\s+mov\s+w0, #(\d+)', a)
print('size', m.group(1) if m else '?')
