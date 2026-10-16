# millet_pkg - EQUIVALENT

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
