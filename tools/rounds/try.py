"""try.py <module> <file> <fn> <old> <new> [<old2> <new2> ...] : replace text (python-escaped, e.g. \t \n) in file, run q.sh with FD=<fn>, print summary, restore.
usage env: FD handled here"""
import sys, subprocess, os
mod, f, fn = sys.argv[1:4]
pairs = [(sys.argv[i].encode().decode('unicode_escape'), sys.argv[i+1].encode().decode('unicode_escape')) for i in range(4, len(sys.argv), 2)]
src = open(f, encoding='utf8').read()
s = src
for o, n in pairs:
    if o not in s:
        print('NOT FOUND', repr(o)); sys.exit(1)
    s = s.replace(o, n, 1)
open(f, 'w', encoding='utf8').write(s)
try:
    out = subprocess.run([os.environ.get("GITBASH","bash"), "g11_tools/q.sh", mod], capture_output=True, text=True, env=dict(os.environ, FD=fn)).stdout
finally:
    open(f, 'w', encoding='utf8').write(src)
err = [l for l in out.splitlines() if 'error' in l][:2]
summ = [l for l in out.splitlines() if l.startswith('=====')]
print(err or summ[:1], flush=True)
if os.environ.get('SHOW'):
    print(out)
