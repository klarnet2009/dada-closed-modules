import re,sys
f=sys.argv[1]
s=open(f,encoding='utf8',newline='').read().replace('\r\n','\n')
s2=re.sub(r'((?:pr_info|pr_err|pr_warn)\("[^"\n]*)\n"',lambda m:m.group(1)+chr(92)+'n"',s)
print('fixed',s.count('\n')-s2.count('\n'))
open(f,'w',encoding='utf8',newline='').write(s2.replace('\n','\r\n'))
