# millet_binder - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 11 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/millet_binder.ko`: compare_ko.py EQUIVALENT (11 functions, 0 exports, 21 imports).

Earlier headline (HyperOS 3 round): millet_binder - EQUIVALENT


Template: XRING `dijun-v-oss` `millet_binder.c` (+ `binder_oem.h`); the dada stock is a newer revision, adapted from its disassembly.
compare_ko: 11 functions byte-identical, relocations, objects, 0 exports, 21 imports + CRCs, modinfo (`depends=binder_gki,millet_core,millet_pkg`): all OK -> EQUIVALENT.
Providers: `millet_core`, `millet_pkg` (our own EQUIVALENT builds) and the stub `re/stubs_portD` (`binder_gki.ko`: `oem_register_binder_hook`, `query_binder_app_stat`, `reset_isfrozen`).
The stub CRCs of `oem_register_binder_hook` / `reset_isfrozen` (they mention `struct binder_proc` / the hook table) are forged with `work/scripts/perfC/crcforge.py` (`#ifdef __GENKSYMS__` stand-in);
`query_binder_app_stat(int)` matches with its real prototype.

Changes relative to the template:
- `oem_trans_hook` has a 6th argument `struct binder_proc *proc` (`type_binder_trans_hook`), forwarded to `reset_isfrozen(uid, pid, found, proc)`; `BINDER_EXTRA_MSG` has a 5th value `BINDER_TRANS_NO_FREEZEBINDER = 4`
  (name from the stock BTF).
- `mi_binder_trans_hook`: extra entry guard `!dst || !dst->stack || !refcount_read(&dst->usage)`; debug print `binder_trans_hook dst=%d srcpid=%d` when `millet_debug && !frozen_task_group(dst) && frozen_task(dst)`;
  if `freezebinder_enable == 1 && !oneway && frozen_task_group(dst)`: `found = find_and_clear_piduid(src uid, src, dst uid)`, `is_frozen = reset_isfrozen(src uid, dst->pid, found, proc)` (+ debug print
  `... no is_frozen`); message type is `BINDER_TRANS_NO_FREEZEBINDER` when `(found && is_frozen == 1) || is_frozen == 2`, else `BINDER_TRANS`.
- `binder_sendmsg`: if the task is frozen and `freezebinder_enable` and `pri[0]` is neither `BINDER_BUFF_WARN` nor `BINDER_TRANS_NO_FREEZEBINDER`: `found = find_and_clear_piduid(caller_uid, src_task, uid)`;
  `if (!found) pri[0] += 111`; debug print `bindersend found=%d srcuid=%d srcpid=%d uid=%d oneway=%d`; a not-found oneway message is dropped (returns 0).
- Lesson (register/phi order): the declaration order of the local variables (`found, flag, is_frozen` vs `found, is_frozen, flag`) decided the order of the constant materialisations after the call: it fixed the last diff.
- `frozen_task_group()` in `millet.h` has no `frozen_task()` term (see millet_sig); `mi_binder_trans_hook` calls `frozen_task(dst)` explicitly.
