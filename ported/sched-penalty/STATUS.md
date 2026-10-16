# sched-penalty - EQUIVALENT (compare_ko.py all OK: 12 functions, 25 imports; modinfo name=sched_penalty, softdeps)
Template: micode_bixi `kernel/sched/walt/penalty.c` (sched-penalty := penalty.o in walt/Makefile), unchanged except the `#include "walt.h"` replaced by `<linux/sched/clock.h>`
(walt.h pulls in the private kernel/sched/sched.h + fs/proc/internal.h via relative paths and nothing from it is used). Android vendor hooks `android_rvh_flush_task`/`before_do_sched_yield` exist in our 6.6.144 headers.
