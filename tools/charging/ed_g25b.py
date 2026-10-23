import sys
def edit(path, old, new, count=1):
    b = open(path,'rb').read()
    crlf = b'\r\n' in b
    s = b.decode('utf-8')
    if crlf:
        s = s.replace('\r\n','\n')
    assert s.count(old)==count, (path, s.count(old))
    s = s.replace(old,new)
    if crlf:
        s = s.replace('\n','\r\n')
    open(path,'wb').write(s.encode('utf-8'))
    print('ok', path, 'CRLF' if crlf else 'LF')
