# sched-penalty - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 12 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/sched-penalty.ko`: compare_ko.py EQUIVALENT (12 functions, 0 exports, 25 imports).

Earlier headline (HyperOS 3 round): sched-penalty - EQUIVALENT (compare_ko.py all OK: 12 functions, 25 imports; modinfo name=sched_penalty, softdeps)

Template: micode_bixi `kernel/sched/walt/penalty.c` (sched-penalty := penalty.o in walt/Makefile), unchanged except the `#include "walt.h"` replaced by `<linux/sched/clock.h>`
(walt.h pulls in the private kernel/sched/sched.h + fs/proc/internal.h via relative paths and nothing from it is used). Android vendor hooks `android_rvh_flush_task`/`before_do_sched_yield` exist in our 6.6.144 headers.
