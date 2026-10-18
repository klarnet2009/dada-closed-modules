import sys, re, collections
sys.path.insert(0, "<work-dir>")
from compare_ko import Elf
a = Elf(sys.argv[1]).functions(); b = Elf(sys.argv[2]).functions()
out = sys.argv[3]
norm = lambda n: re.sub(r"\.llvm\.\d+$", "", n)
na = {norm(n): n for n in a}; nb = {norm(n): n for n in b}
diff = [k for k in na if k in nb and a[na[k]][0] != b[nb[k]][0]]
rel = [k for k in na if k in nb and a[na[k]][0] == b[nb[k]][0] and a[na[k]][1] != b[nb[k]][1]]
open(out + ".bytes_diff.txt", "w").write("\n".join("%s %d %d" % (k, len(a[na[k]][0]), len(b[nb[k]][0])) for k in diff))
open(out + ".reloc_diff.txt", "w").write("\n".join(rel))
open(out + ".only_stock.txt", "w").write("\n".join(sorted(set(na) - set(nb))))
open(out + ".only_built.txt", "w").write("\n".join(sorted(set(nb) - set(na))))
pref = collections.Counter(re.match(r"[a-z]+_?[a-z0-9]*", k).group(0) if re.match(r"[a-z]+_?[a-z0-9]*", k) else k for k in diff)
print("bytes diff", len(diff), "reloc-only diff", len(rel)); print(pref.most_common(25))
