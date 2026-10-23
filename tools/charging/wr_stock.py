import re,sys,collections
T={'.text+0x9cbc':'MACW','.text+0x21cc':'BLKW','.text+0x9a6c':'WORDW','.text+0x9e94':'BYTEW','.text+0x8f38':'SEAL','.text+0x19c5c':'TOGGLE_CO','.text+0x9588':'FUD_W'}
fn=None; lastw1=None; rows=collections.OrderedDict()
lines=open(sys.argv[1],encoding='utf-8',errors='replace').read().split('\n')
for i,l in enumerate(lines):
    m=re.match(r'^[0-9a-f]+ <(\S+)>:',l)
    if m: fn=m.group(1); w1=None; continue
    m=None if 'R_AARCH64_' in l else re.match(r'^\s+[0-9a-f]+:\s+(\w+)\s+(.*?)(\s*//.*)?$',l)
    if m:
        op,args=m.group(1),m.group(2)
        mm=re.match(r'w1, #(0x[0-9a-f]+|-?\d+)$',args)
        if op=='mov' and mm: w1=hex(int(mm.group(1),0))
        elif re.match(r'w1\b',args) and op in('ldrb','ldr','mov','and','orr','add'): w1=op+' '+args
        elif op in('movk',) and args.startswith('w1'): w1=(w1 or '?')+' movk '+args
        continue
    m=re.search(r'R_AARCH64_(?:CALL|JUMP)26\s+(\S+)',l)
    if m and m.group(1) in T:
        rows.setdefault((fn,T[m.group(1)]),[]).append(w1)
    elif m and m.group(1)=='i2c_transfer' : rows.setdefault((fn,'I2C_TRANSFER'),[]).append(None)
    elif m and m.group(1).startswith('i2c_smbus_write'): rows.setdefault((fn,m.group(1)),[]).append(None)
    if m: w1=None
for (f,t),v in rows.items(): print(f,t,collections.Counter(map(str,v)))
