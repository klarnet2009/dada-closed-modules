# dynamic_readahead - EQUIVALENT (compare_ko.py all OK: 8 functions, 16 imports, 0 exports)
Reference: OS4 stock `vendor_ramdisk/dynamic_readahead.ko` (6.6.57). Built with Kleaf DDK + ThinLTO defconfig (`bz.sh`), no hacks (no forged CRC, no padcrc).
Template: XRING `work/re/xiaomi-modules/mi_dynamic_readahead/dynamic_readahead.c` (itself the Oplus module) for the zone/high_wm/readahead/readaround core.
Recovered from the disassembly (not in any public tree): cgroup-name filter (`task_css(current, cpuset_cgrp_id)->cgroup->kn->name` == "background"),
`!rt_task(current)` instead of XRING's qos check; `ra_close_large_folio_enable` hook `android_vh_page_cache_ra_order_bypass` (bypass on EROFS sb magic 0xE0F5E1E2);
`android_vh_should_fault_around` hook (uid<10000 -> no fault-around; appid=uid%10000 < 512 looked up in a 512-bit bitmap); module param `fault_around` (write uid, 0 clears; read lists uids);
init registers 4 hooks with exact unwinding, a should_fault_around registration failure is non-fatal.
Only codegen subtlety: block order of `fault_around_set` (early `return`s, `-EINVAL` last) - see the final source.
Tooling: `work/re/src/g1_tools/{q.sh,bz.sh,mkpkg.sh,cdis.py,try.py}` (tag g1, server scratch <work-dir>).
