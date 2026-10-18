"""kcb.py <hexhash>... [-n N] [-t mangled]... [-s structname]... : brute-force the C prototype whose kCFI hash (xxh64 of the Itanium mangled function type, low 32 bit, no integer normalisation) matches.
Returns v/ints/pointer types; up to N args (default 3)."""
import sys, itertools
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from xxh import kh
args = sys.argv[1:]; n = 3; extra = []; structs = []; want = set(); i = 0
while i < len(args):
    if args[i] == '-n': n = int(args[i+1]); i += 2
    elif args[i] == '-t': extra.append(args[i+1]); i += 2
    elif args[i] == '-s': structs.append(args[i+1]); i += 2
    else: want.add(int(args[i], 16)); i += 1
S = ['task_struct','rq','cpufreq_policy','work_struct','hrtimer','ctl_table','file','inode','seq_file','irq_work','cpumask','list_head','kobject','delayed_work','device','cred','user_struct','mm_struct','module','kobj_attribute','device_attribute','pt_regs','sk_buff','sock','net_device','page','vm_area_struct','cpu_topology','notifier_block','walt_task_struct','walt_rq','metis_choose_cpu_args_g','mi_game_task','frame_load_items','kernel_param']+structs
T = ['b','c','h','s','t','i','j','l','m','x','y','v_']
base = [t for t in T if t != 'v_'] + ['Pv','PKc','Pc','Pi','Pj','Pm','Py','Pb','PKv','PPv','Pl','Ph'] + ['P%d%s' % (len(s), s) for s in S] + ['PK%d%s' % (len(s), s) for s in S] + extra
rets = ['v'] + base
cnt = 0
for r in rets:
    for k in range(0, n + 1):
        for a in itertools.product(base, repeat=k):
            m = '_ZTSF' + r + (''.join(a) if k else 'v') + 'E'
            h = kh(m)
            cnt += 1
            if h in want: print('%#x %s' % (h, m))
print('tried', cnt)
