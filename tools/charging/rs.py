import sys
d=open('tmp/stock_bq.ko','rb').read()
ro=0x86d8
for a in sys.argv[1:]:
    o=int(a,16); e=d.index(b'\0',ro+o); print(hex(o),repr(d[ro+o:e].decode('latin1')))
