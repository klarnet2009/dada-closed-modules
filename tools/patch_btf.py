import sys
p = sys.argv[1]
s = open(p).read()
key = '    print("\\n/* ---- types reachable from the module ---- */")'
assert key in s, 'key not found'
add = '''    if "--all" in sys.argv:
        for i, t in enumerate(mod.types):
            if t["kind"] in ("struct", "union", "enum", "enum64") and t["name"] and not t["name"].startswith("?"):
                seen.add(mod.start_id + i)
'''
s = s.replace(key, add + key, 1)
open(p, 'w').write(s)
print('patched')
