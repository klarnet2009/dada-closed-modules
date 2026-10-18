import itertools,sys
words="d dtask task item info node entry list record detect check state data process monitor timeout dt ignore pm proc pid hung hang cfg config rec elem element unit param obj ctx desc d_state dstate stat status mi miui xm xiaomi uninterruptible sleep block blocked thread tsk time item_info dinfo key name comm group leader".split()
out=set()
for k in (1,2,3):
    for c in itertools.product(words,repeat=k):
        for sep in ('_',''):
            n=sep.join(c)
            out.add(n)
with open('gnames.txt','w') as f:
    for n in sorted(out): f.write('P%d%s\n'%(len(n),n))
print(len(out))
