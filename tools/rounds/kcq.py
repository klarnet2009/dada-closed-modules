import sys, itertools
sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from xxh import kh
want = {int(x, 16) for x in sys.argv[2:]}; n = int(sys.argv[1])
ints = ['i','j','m','l','y','x','b','h','t','c','s','a']
structs = ['mem_cgroup','cma_align','cma','page','zone','task_struct','cpumask','proc_dir_entry','rq','work_struct','hrtimer','cpufreq_policy','list_head','mi_game_task','mi_plat_user_struct','user_struct','cred','mm_struct','file','inode','seq_file','ctl_table','rb_node','frame_load_items','mi_task_struct','plat_task_struct','walt_task_struct']
ptrs = ['Pv','PKv','Pc','PKc','Pi','Pj','Pm','Py','Pb','Ph'] + ['P%d%s' % (len(x), x) for x in structs] + ['PK%d%s' % (len(x), x) for x in structs]
base = ints + ptrs
rets = ['v'] + base
for r in rets:
    for k in range(0, n + 1):
        for a in itertools.product(base, repeat=k):
            m = '_ZTSF' + r + (''.join(a) if k else 'v') + 'E'
            if kh(m) in want: print('%#x %s' % (kh(m), m))
