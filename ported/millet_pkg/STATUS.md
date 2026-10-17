# millet_pkg - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 -> OS4 stock changed 7 of 11 functions: the uid_rec[64] array (and the core_uid_rec[74] array exported by millet_core) is replaced by the uid hash tables of the OS4 millet_core (imports core_uid_hash_{add,del,find}, uid_rec_hash_{add,del,find_and_clear,clear,show}; the -alt_cb_patch_nops and -core_uid_rec imports are gone). Recovered from the OS4 disassembly: pkg_stat_show = uid_rec_hash_show(m, "\t") + "\n"; pkg_ip4_in/pkg_ip6_in call uid_rec_hash_find_and_clear(uid) (the local find_and_clear_uid is removed); pkg_recv_hook: ADD_UID/DEL_UID -> uid_rec_hash_add/del, ADD_CORE_UID -> ret = core_uid_hash_add(uid) with the log "%s: core_uid_hash_add failed for uid %d, ret=%d" (uid re-read from the payload) when ret < 0, DEL_CORE_UID -> core_uid_hash_del, CLEAR_ALL_UID now only calls uid_rec_hash_clear() (pkg_clear_all() with the freezebinder_enable / core table resets is gone); find_and_clear_piduid uses core_uid_hash_find for dstuid and srcuid (ret = the find result is returned for the srcuid case) and the two black-list loops lost their unlikely() (needed for the stock block layout); pkg_add_uid is now an out-of-line static (uid, rec, max) used only for core_black_pid_rec/core_black_uid_rec; millet_pkg_init no longer clears uid_rec. Real Kleaf build (together with the OS4 millet_core) vs OS4 stock vendor_ramdisk/millet_pkg.ko: compare_ko.py EQUIVALENT (12 functions, 1 export, 28 imports); data objects identical. The OS3 source is kept as g6/millet_pkg_os3_backup.c.

Earlier headline (HyperOS 3 round): millet_pkg - EQUIVALENT


Template: XRING `dijun-v-oss` `millet_pkg.c` (older revision; the dada stock is newer), reconstructed to the stock disassembly. compare_ko: 11 functions identical, relocations, objects, 1 export
(`find_and_clear_piduid`, CRC matches => prototype `int (uid_t srcuid, struct task_struct *src, uid_t dstuid)`), 22 imports + CRCs, modinfo (`depends=millet_core`): all OK -> EQUIVALENT.

What the stock adds relative to the template (all recovered from disassembly, no template for it):
- `pkg_recv_hook` handles `ADD_CORE_UID=100 / DEL_CORE_UID / ADD_CORE_BLACK_PID / DEL_CORE_BLACK_PID / ADD_CORE_BLACK_UID / DEL_CORE_BLACK_UID` (enum in `uapi/millet.h`) on
  `core_uid_rec[74]`, `core_black_pid_rec[10]`, `core_black_uid_rec[10]` (exported by millet_core); uid 111 is a magic value: ADD sets `freezebinder_enable = 1`, sends a `PKG_TYPE` message with
  `pkg_owner = 111` and logs `freezebinder_enable=%d : set!`; DEL clears it.
- `pkg_add_uid(rec, max, uid)` / `pkg_del_uid(rec, max, uid)` are one generic always-inlined helper used for all four tables (full unroll for max=10 only, as the compiler decides). `pkg_clear_all()` also clears
  `freezebinder_enable` and the three core tables. `init` sets `freezebinder_enable = 0` first.
- `find_and_clear_piduid()` (exported, used by millet_binder): srcuid==0 or !freezebinder_enable -> 1; dstuid in core_uid_rec -> 1; `src->pid` (read once into a local) in core_black_pid_rec -> 0;
  appid = srcuid % 100000: if appid >= frozen_uid_min -> srcuid in core_uid_rec ? 1 : 0, else appid in core_black_uid_rec -> 0, else 1 (3 debug prints under `millet_debug`).
- KEY for the block layout: every "found" test is `unlikely(atomic_read(&rec[i]) == x)` (the found/print blocks then sit at the end of the function, exactly like the stock). `if (millet_debug)` stays plain.
