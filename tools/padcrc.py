"""padcrc.py <module> <export symbol> <stock crc hex> <PLACEHOLDER> [file.c]   (a17A copy of scripts_mcaB/padcrc.py)
Replaces PLACEHOLDER in work/re/src/stubs_a17A/include/** and in the module dir by a CRC-forged identifier (GENKSYMS-ONLY pad)
so that genksyms reproduces the stock export CRC.  Run from anywhere; uses the server's genksyms (mcaB_work/gkd.sh)."""
import glob, io, os, re, subprocess, sys, tarfile
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from forcecrc import force
RE = os.path.join(HERE, "..")
KEY = os.path.join(RE, "..", "..", "<ssh-key-file>")
K = ["ssh", "-i", KEY, "-o", "BatchMode=yes", "-o", "ConnectTimeout=20", "<user>@<build-server>"]
W = "<work-dir>"
mod, sym, target, ph = sys.argv[1], sys.argv[2], int(sys.argv[3], 16), sys.argv[4]
cfile = sys.argv[5] if len(sys.argv) > 5 else [os.path.basename(f) for f in sorted(glob.glob(os.path.join(RE, "src", mod, "*.c")))][0]
buf = io.BytesIO()
with tarfile.open(fileobj=buf, mode="w") as tf:
    tf.add(os.path.join(RE, "src", "stubs_a17A", "include"), arcname="inc")
    tf.add(os.path.join(RE, "src", mod), arcname="mod")
subprocess.run(K + ["rm -rf %s && mkdir -p %s && tar x -C %s" % (W, W, W)], input=buf.getvalue(), check=True)
out = subprocess.run(K + ["cd %s && bash <build-dir> mod/%s %s/inc %s/mod 2>&1" % (W, cfile, W, W)], capture_output=True, text=True).stdout
m = re.search(r"Export " + re.escape(sym) + r" == <(.*)>", out)
if not m:
    sys.exit("no export stream for %s:\n%s" % (sym, out[:1500]))
stream = m.group(1)
if ph not in stream:
    sys.exit("placeholder %s not in stream:\n%s" % (ph, stream))
pre, suf = stream.split(ph, 1)
ident = force(pre, suf, target)
n = 0
for f in glob.glob(os.path.join(RE, "src", "stubs_a17A", "include", "**", "*.h"), recursive=True) + glob.glob(os.path.join(RE, "src", mod, "**", "*.[ch]"), recursive=True):
    t = open(f, newline="").read()
    if ph in t:
        open(f, "w", newline="").write(t.replace(ph, ident)); n += 1; print("patched", f)
print(ph, "->", ident, "(%d files)" % n)
