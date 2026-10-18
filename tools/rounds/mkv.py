import json,re,sys
BS=chr(92)
def fix(s): return s.replace('@NL@',BS+'n')
def save(find, alts, path):
    json.dump([{"find":fix(find),"alts":[fix(a) for a in alts]}], open(path,'w'))
def cur(fn_regex, path='mi_reclaim/mi_reclaim.c'):
    src=open(path,encoding='utf8').read()
    return re.search(fn_regex, src, re.S).group(0)
