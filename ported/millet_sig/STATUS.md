# millet_sig - EQUIVALENT

Template: XRING `dijun-v-oss` `millet_sig.c` (+ headers). compare_ko: 4 functions identical, 1 export (`millet_sig`) + CRC, 13 imports + CRCs, modinfo: all OK -> EQUIVALENT.
Change vs template: `millet.h` `frozen_task_group()` is `freezing(task) || cgroup_task_freeze(task) || cgroup_task_frozen(task)` (the stock has no `__state & TASK_FROZEN` test; with it the compiled `signals_sendmsg`
has an extra `ldar/tbnz #0xf`). `frozen_task()` kept as `static inline` (unused, otherwise -Werror=unused-function).
Needs the kernel exports `freezer_active`, `freezing_slow_path`, `__tracepoint_android_vh_do_send_sig_info` (all in GKI 6.6.144).
