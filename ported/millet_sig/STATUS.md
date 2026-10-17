# millet_sig - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 4 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/millet_sig.ko`: compare_ko.py EQUIVALENT (4 functions, 1 exports, 13 imports).

Earlier headline (HyperOS 3 round): millet_sig - EQUIVALENT


Template: XRING `dijun-v-oss` `millet_sig.c` (+ headers). compare_ko: 4 functions identical, 1 export (`millet_sig`) + CRC, 13 imports + CRCs, modinfo: all OK -> EQUIVALENT.
Change vs template: `millet.h` `frozen_task_group()` is `freezing(task) || cgroup_task_freeze(task) || cgroup_task_frozen(task)` (the stock has no `__state & TASK_FROZEN` test; with it the compiled `signals_sendmsg`
has an extra `ldar/tbnz #0xf`). `frozen_task()` kept as `static inline` (unused, otherwise -Werror=unused-function).
Needs the kernel exports `freezer_active`, `freezing_slow_path`, `__tracepoint_android_vh_do_send_sig_info` (all in GKI 6.6.144).
