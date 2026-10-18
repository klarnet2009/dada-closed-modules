import sys
for p in sys.argv[1:]:
    s=open(p,'rb').read().replace(b'\r\n',b'\n').replace(b'\n',b'\r\n'); open(p,'wb').write(s)
