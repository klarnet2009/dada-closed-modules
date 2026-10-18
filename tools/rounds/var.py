"""var.py <module> <file> <func-for-summary> <start-marker> <end-marker> <variantfile...>
Replace the text between the markers (start inclusive, end exclusive) by each variant, run q.sh, print the summary line of <func>.
Leaves the original file untouched. Run from work/re/src. Env DEFS/INCS are passed on to q.sh."""
import sys, subprocess, os
mod, f, fn, a, b = sys.argv[1:6]
src = open(f, encoding='utf8').read()
i = src.index(a); j = src.index(b, i)
for vf in sys.argv[6:]:
    v = open(vf, encoding='utf8').read()
    open(f, 'w', encoding='utf8').write(src[:i] + v + src[j:])
    out = subprocess.run([os.environ.get("GITBASH","bash"), "g11_tools/q.sh", mod], capture_output=True, text=True, env=dict(os.environ, FD=fn)).stdout
    err = [l for l in out.splitlines() if 'error' in l][:2]
    summ = [l for l in out.splitlines() if l.startswith('=====') or l.startswith(fn)]
    print('##', os.path.basename(vf), err or summ[:1], flush=True)
open(f, 'w', encoding='utf8').write(src)
