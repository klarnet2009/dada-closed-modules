import sys, time
sys.path.insert(0, "<work-dir>")
from compare_ko import Elf
t = time.time()
a = Elf(sys.argv[1]).functions(); b = Elf(sys.argv[2]).functions()
common = [n for n in a if n in b]
same = [n for n in common if a[n][0] == b[n][0]]
same_rel = [n for n in same if a[n][1] == b[n][1]]
# promoted-name normalisation: strip .llvm.<hash>
import re
norm = lambda n: re.sub(r"\.llvm\.\d+$", "", n)
na = {norm(n): n for n in a}; nb = {norm(n): n for n in b}
common2 = [k for k in na if k in nb]
same2 = [k for k in common2 if a[na[k]][0] == b[nb[k]][0]]
print("stock %d built %d | common by name %d, bytes-identical %d, bytes+relocs identical %d | common after .llvm normalisation %d, bytes-identical %d | only stock %d only built %d (%.1fs)" % (
    len(a), len(b), len(common), len(same), len(same_rel), len(common2), len(same2), len(set(na) - set(nb)), len(set(nb) - set(na)), time.time() - t))
