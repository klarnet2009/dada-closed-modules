#include <linux/module.h>
#include <linux/sched.h>
#include <linux/stddef.h>
#ifdef CONFIG_SCHEDSTATS
char schedstats_on[1];
#endif
#ifdef CONFIG_SCHED_INFO
char schedinfo_on[1];
#endif
#ifdef CONFIG_PSI
char psi_on[1];
#endif
char canary_off[offsetof(struct task_struct, stack_canary) - 1400];
