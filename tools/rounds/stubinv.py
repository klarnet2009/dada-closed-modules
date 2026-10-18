"""stubinv.py <mod> ...: per module, compare function symbol SIZES of the newest built .ko (any <work-dir>) with the OS4 stock .ko.
Prints: stock/built paths, #stock funcs, #missing, #size-stubbed (built < 40% of stock, stock >= 32 B), bytes of missing+stubbed, top offenders."""
import glob, os, re, subprocess, sys
B = '<build-dir>'
def funcs(path):
    out = subprocess.run([B, '-t', path], capture_output=True, text=True, timeout=300).stdout
    d = {}
    for l in out.split('\n'):
        m = re.match(r'^([0-9a-f]{16}) (.{7}) (\S+)\s+([0-9a-f]{16}) (\S+)$', l)
        if not m or 'F' not in m.group(2): continue
        name = re.sub(r'\.llvm\.\d+$', '', m.group(5)); sz = int(m.group(4), 16)
        k = 0
        while (name, k) in d: k += 1
        d[(name, k)] = sz
    return d
def stock_of(mod):
    c = glob.glob('<work-dir>' % mod, recursive=True) + glob.glob('<work-dir>' % mod)
    if c: return c[0], 'OS4'
    c = sorted(glob.glob('<work-dir>' % mod), key=os.path.getmtime)
    return (c[-1], 'OS3/other') if c else (None, None)
def built_of(mod):
    c = sorted(glob.glob('<work-dir>' % mod) + glob.glob('<work-dir>' % mod), key=os.path.getmtime)
    return c[-1] if c else None
for mod in sys.argv[1:]:
    s, kind = stock_of(mod); b = built_of(mod)
    if not s or not b:
        print('%s | no-stock-or-built (stock=%s built=%s)' % (mod, s, b)); continue
    try: fs, fb = funcs(s), funcs(b)
    except Exception as e:
        print('%s | ERR %s' % (mod, e)); continue
    miss = [(fs[n], n) for n in fs if n not in fb]
    stub = [(fs[n], n, fb[n]) for n in fs if n in fb and fs[n] >= 32 and fb[n] < 0.4 * fs[n]]
    small = [(fs[n], n, fb[n]) for n in fs if n in fb and fs[n] >= 24 and fb[n] <= 8]
    tot = sum(x[0] for x in miss) + sum(x[0] - x[2] for x in stub)
    top = sorted([(x[0], x[1], 'missing') for x in miss] + [(x[0], x[1], 'built %dB' % x[2]) for x in stub], reverse=True)[:5]
    print('%s | %s | stockfn %d | missing %d | size-stubbed %d (tiny<=8B %d) | bytes %d | built %s age %dh | top: %s' % (mod, kind, len(fs), len(miss), len(stub), len(small), tot, os.path.basename(os.path.dirname(os.path.dirname(b))), (__import__('time').time() - os.path.getmtime(b)) / 3600, '; '.join('%s %dB %s' % (n[0] + ('#%d' % n[1] if n[1] else ''), sz, w) for sz, n, w in top)))
