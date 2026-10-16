#include <linux/module.h>
#include <linux/sched.h>
#include <linux/sched/rt.h>
#include <linux/string.h>
void a(void); void b(void); void c(void);
void t1(struct task_struct *p) { if (rt_task(p)) a(); else b(); }
void t2(struct task_struct *p) { if (likely(p->prio > 99)) { if (strncmp(p->comm, "x", 1)) b(); else a(); } else a(); }
void t3(struct task_struct *p) { if (p->prio > 99) { if (strncmp(p->comm, "x", 1)) return; } a(); }
void t4(struct task_struct *p) { if (p->prio > 99 && strncmp(p->comm, "x", 1)) return; a(); }
void t5(struct task_struct *p) { if (!(p->prio > 99) || !strncmp(p->comm, "x", 1)) a(); b(); }
void t6(struct task_struct *p) { if (p->prio < 100 || !strncmp(p->comm, "x", 1) || !strncmp(p->comm, "y", 1)) a(); b(); }
void t7(struct task_struct *p) { if (p->prio <= 99) { a(); return; } if (!strncmp(p->comm, "x", 1)) { a(); return; } b(); }
void t8(struct task_struct *p) { if (p->prio > 99) { if (!strncmp(p->comm, "x", 1)) goto yes; return; } yes: a(); }
void t9(struct task_struct *p) { c(); if (unlikely(p->prio < 100)) goto yes; if (!strncmp(p->comm, "x", 1)) goto yes; return; yes: a(); }
void t10(struct task_struct *p) { if (!rt_task(p) && strncmp(p->comm, "x", 1) && strncmp(p->comm, "y", 1)) return; a(); }
