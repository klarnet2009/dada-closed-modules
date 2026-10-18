# mitee_dlkm review 1 (review-mitee-opus1, 18.10.2026)

Independent review of `work/re/src/mitee_dlkm/` against the OS4 stock `<work-dir>`.
Method: `llvm-objdump -dr` of the stock with relocations folded in and `.rodata` strings resolved (server `<work-dir>`,
tools `annot.py`, `rob.py` in the same directory); every function of `mitee_msgq.c` read instruction by instruction against our C;
the tee_core/tee_shm/TUI/memlog paths checked at the places that validate user input. No build was run (the source was read as it is on disk
at the time of the review; goal-G12d may be editing the same files). Addresses below are `.text` offsets of the stock.

## A. Defects in OUR code that the stock does not have (ranked)

1. **MEDIUM - `mitee_msgq_driver` lacks `.probe_type = PROBE_FORCE_SYNCHRONOUS`** (mitee_msgq.c:1124-1131).
   Evidence: the stock object `mitee_msgq_driver` = `.data+0x40` (address passed to `__platform_driver_register` at 0x584c);
   byte `+0x54` = 2. In 6.6 `struct platform_driver` the `struct device_driver` starts at +0x30 and `probe_type` is at +0x24 inside it
   (+0x54 total); value 2 = `PROBE_FORCE_SYNCHRONOUS`. This is the "unexplained byte 2 at +0x54" of STATUS G12c.
   Failure scenario: if asynchronous probing is requested for the module (`async_probe` module parameter, `driver_async_probe=` /
   `module.async_probe` on the command line), `comm_ops->init()` returns before `mitee_msgq_probe` has set `g_msgq_dev`;
   `optee_shm_memremap` then gets -1 from `mitee_msgq_round_trip_call` ("driver didn't initialized") and module init fails.
   The stock is immune because the probe is forced synchronous. Fix: add the field (also closes the objects FAIL in compare_ko_v2).
2. **LOW - `mitee_msgq_secmem_lend`: leak of the sgt and its pages when the ACL kmalloc fails** (mitee_msgq.c:873-875).
   Stock (0x5618 -> 0x56d0 -> 0x56e4): the failure goes through the revert path, which ends in `mitee_free_memory_sgt(size, sgt)`;
   ours returns -ENOMEM directly and the caller (`mitee_msgq_rpc_callback`, 0xffff0006 path) does not free either.
   OOM-only path. Fix: `ret = -ENOMEM; goto revert;` (the stock also issues the useless revert hyp_assign_table; harmless).
3. **INFO - deviations that are safer than the stock, keep or document**:
   - `mitee_msgq_shm_register` ACL kmalloc failure (mitee_msgq.c:704-708): stock (0x4798 -> 0x49cc) calls `ghd_rm_mem_reclaim(0,0)` and the
     revert `hyp_assign_table` on a table that was never assigned; ours skips both. Behaviour differs only on OOM.
   - Revert `hyp_assign_table` in the shm_register error path (mitee_msgq.c:770): stock logs a failure ("assign_sgt_perm", 0x4a80), ours is silent.
   - `mitee_msgq_rpc_callback` (mitee_msgq.c:938-956): stock compares only the low 32 bits of `value->a` and `value->b` (`ldr w9,[x0]`, `ldr w8,[x0,#8]`),
     ours compares 64 bits. Differs only for out-of-spec values from the TEE.
   - `tui_shm_create_fd` (tui_shm.c:155-160): on `anon_inode_getfd` failure ours unlocks `tuiMutex`; the stock (0xcc5c -> 0xcc1c) returns with the
     mutex still held. Ours is better; it is a behaviour difference.

No out-of-bounds, use-after-free, double free or missing NULL check was found in our `mitee_msgq.c` that the stock does not also have.

## B. Stock bugs reproduced bug-compatibly (do not fix for equivalence; know them before any device test)

| # | Where (ours) | Stock evidence | Problem |
|---|---|---|---|
| S1 | mitee_msgq_remove, mitee_msgq.c:1086-1117 | 0x6198-0x62bc | No `kthread_stop` of the recv thread, no `gh_rm_unregister_notifier`, no `gh_msgq_unregister`, no `dma_free_attrs`; rx/f_data are freed while the thread may still use them; with `shared-buffer` they leak; `g_msgq_dev` keeps pointing at the devm-freed qdev. Unloading the module or unbinding the device is unsafe (stale callbacks into freed module text). |
| S2 | mitee_tui_exit, mitee_tui.c:307-317 | 0xc284 | Devices/classes `tui_cls`, `tui_supp_cls` never destroyed, region unregistered before `cdev_del`: after rmmod the /dev nodes remain and a reload fails. |
| S3 | mitee_msgq_rpc_callback FREE_SECMEM, mitee_msgq.c:958-983 | 0x6a54-0x6ab8 | No found-check: an unknown handle from the TEE makes `sm` the list-head container (qdev+0x1b8), then `hyp_assign_table`/`kfree` on a non-object. TEE-triggered. |
| S4 | mitee_msgq_secmem_lend, mitee_msgq.c:904-917 | 0x5770 -> 0x56bc | kmalloc failure of the list entry returns 0 without setting `*handle_out`; the TEE gets handle 0 with success while the memory was reverted and freed. |
| S5 | mitee_msgq_shm_register, mitee_msgq.c:722-725 | 0x47f4 -> 0x4a9c -> 0x47f8 | `hyp_assign_table` failure only logged, sharing continues. A failed revert (0x4a80) also only logs, then the pages go back to the caller while their stage-2 ownership is unknown. |
| S6 | mitee_msgq_shm_unregister(_supp), mitee_msgq.c:799-822, 838-856 | 0x4f44/0x54d4 | Unknown handle -> `sgt = NULL` passed to `hyp_assign_table`/`sg_free_table`. Only reachable if the rhashtable and `sec_world_id` get out of sync. |
| S7 | mitee_msgq_probe, mitee_msgq.c:1017-1069 | 0x5e84-0x6078 | rx/f_data kzalloc'ed (not devm) leak on every error return; `of_node` from `shared-buffer` never put; `gh_msgq_register` result not checked with IS_ERR (an ERR_PTR, e.g. -EBUSY when label 5 is taken, is later dereferenced by the recv thread). |
| S8 | mitee_msgq_get_vm_state / from_msg_param | 0x5898, 0x45c4 | `g_msgq_dev` dereferenced without NULL check (only safe because init fails when probe did not run). |
| S9 | round_trip_call / recv thread | 0x5b08, 0x59b0 | `wait_for_completion` without timeout: a secure VM that stops answering hangs the caller in D state forever. |
| S10 | tui_shm_create_fd, tui_shm.c:151-154 | 0xcba8-0xcc58 | The "invalid name/flags" path returns with `tuiMutex` held (all later TUI shm calls block) and leaks the shm. |
| S11 | tui_shm_create_fd + tui_ioctl_tui_shm_alloc, tui_shm.c:151-162 | 0xcbdc-0xcc08, 0xc6a0 | `name[16]` copied from user space is not NUL-terminated before `strlen`/`memcpy` into the 16-byte `shm->name`; only a fortify "field-spanning write" WARN (bug table tui_shm.c:154), the copy still happens. Fix would be one `data.name[15] = 0`. |
| S12 | tui_shm fd lifetime, tui_shm.c:83-137 | 0xcae0, 0xcdac, 0xce5c | The anon fd keeps a raw `shm` pointer without a reference; `TUI_IO_FREE_SHM` frees it while the fd stays open (later mmap/release touch freed memory). |
| S13 | tui_process_tuisvc_callback GET_FONT_BUFFER, mitee_tui.c:168-210 | 0xbc70-0xbe40 | `size_in` is never compared (stock does not even keep x2), offset is not checked against the font size, `font_buffer` may be NULL for offset != 0. TEE-controlled input. |
| S14 | tee_ioctl SHM_ALLOC "mem" path, tee_core.c:303-320 | 0x9668 (`vmalloc`, no memset) -> `tee_shm_register` | User-sized `vmalloc` (not zeroed) that is mmapped back to user space: stale kernel memory visible to the caller; no size cap. |
| S15 | tee_shm_register, tee_shm.c:237-241 | 0xab9c | `int num_pages` from a user `length` without overflow/cap checks (old upstream form); `shm->size = length` is what later bounds-checks use. |
| S16 | /proc/mitee_log_key, mitee_memlog.c:338-361, 504 | 0x29d8 (mode 0x124), 0x34e0 (345 bytes) | Created 0444; `key_read` ignores `size` and `*ppos` and always copies 345 bytes. On the stock ROM only SELinux protects the key. |

## C. Verified equal to the stock (instruction-level reading)
- comm_ops table `.rodata+0x29a0`: id 1 (QCOM_QMI), init, deinit, get_vm_state, call, from/to_msg_param, shm_register, shm_unregister, shm_unregister_supp - same order and relocations.
- rhashtable params `.rodata+0x29f0`: key_len 8, key_offset 0x10, head_offset 0x18, automatic_shrinking, no hash functions; entry kmalloc(32) {shm, sgt, u64 handle, node}.
- probe: devm_kzalloc 0x210; rx kzalloc 0x538 (size 32 at +8, memset 0x10..0x514, completion +0x518); f_data kzalloc 0x88 (mutex +0x30, lock +0x60, completion +0x68); DT props `gunyah-shm-label`, `xiaomi,master` (-EINVAL), `shared-buffer` (else 4 MiB `dma_alloc_attrs` FORCE_CONTIGUOUS), `peer-name` (0 on failure); notifier priority INT_MAX; `gh_msgq_register(5)`; kthread "mitee_msgq_recv_task"; init order of mutexes/list.
- rm_cb: cmd 0x56100008, status 2 (READY) / 11 (RESET), vmid checks, state 1, share_static_mem, callbacks (0,0) and (0,1); unshare on RESET.
- share_mem_contiguous / unshare: perms 6 (RW) forward, 7 back, src masks, ACL 12 B, SGL 22 B (`gh_sgl_desc` entries at +6), error prints and revert.
- recv_task_fn: 240-byte buffer, error print with `ret`, magic, len <= 240, byte jump table types 0..5 (0 ctrl, 1 "should not reach here", 2/3 default, 4 yield reply, 5 fastcall reply), ctrl msg_id 2/1/0 chain, `ids>>16 == 0x8000`, handshake 5 ms (`__const_udelay(0x147aeb8)`), static-shm and msg-buffer messages (cmd 5 / 12, 0x50 bytes), state transitions 2/3, ring push with full check, fastcall mailbox with `valid` flag, return of the last status.
- round_trip_call: bit-31 split, local fastcalls 10/11/12, state < 2 -> -1, message layout (0x50 B, ids 0x8000), fastcall mutex/second f_data load, ring pop with zeroing, reply copy.
- from/to_msg_param packing (offs_lo u32 @8, offs_hi u16 @0xc, `tee_shm` field @0x20 -> @0xe, size @0x10, shm_ref @0x18 = `sec_world_id` @0x48, -EINVAL for offs >> 48).
- shm_register / unregister / unregister_supp / rem_mem_handle / secmem_lend / rpc_callback / sgt_to_gh_sgl_desc: allocation sizes, flags, vmid/perm arrays, call order, return values (except A2/A3 above).
- tui_ioctl_tui_shm_alloc/free, tui_shm_alloc/free/create_fd: same checks and order (except A3 last bullet). memlog proc modes and key length equal.

## D. kCFI / trap / prototype notes
- `__builtin_trap()` for `!qdev` in recv_task_fn = stock `brk #1`; unreachable (probe always passes qdev).
- Stock has UBSAN bounds traps (`brk #0x5512`, index < 33) on the rx ring; our ring index is always `% 32`, so no trap in either.
- Indirect calls (`optee->ops->do_call_with_arg` hash 0xfea32b1e, rpc callbacks, comm_ops) are all inside the module; preambles match the stock 160/160 per G12c, so a kCFI trap would need a prototype mismatch inside our own sources: none found in the msgq/core/TUI paths read.
- Guesses that remain guesses: enum tee_state names; field names of `struct mitee_msgq_dev`/`mitee_rmem_wire`; the wire format (consistent between our sender and receiver code and the stock, but never checked against a TEE-side peer).

## E. Verdict
- Loading on a test phone with the stock DT and the secure VM ABSENT: expected to load. The probe succeeds from DT data only; `gh_msgq_register(5)` succeeds
  (no other user of label 5 assumed); the recv thread sleeps interruptibly in `gh_msgq_recv` (no rx capability); no RM READY notification ->
  `vm_state` stays 0. Init then uses only LOCAL fastcalls (10 shm config, 11 log buffer), memremaps the shared buffer, registers /dev/tee0 and
  /dev/teepriv0 (and /dev/mitee-tui* if `tui-supported`), `/proc/mitee_log*`, the worker thread. Every TEE request returns -EAGAIN ("tee didn't bootup done"),
  `mitee_smc_notify_*` return errors; nothing blocks at load. Preconditions: synchronous probe (fix A1, or no async probe), the stub providers replaced by the
  real gh_rm_drv/gh_msgq/secure_buffer/qcom-scm/minidump modules (never ship the os4_* stubs), and NO rmmod (S1/S2).
- Without the DT node `xiaomi,mitee-hlos-ipc` the module refuses to load (-EINVAL) the same way the stock does.
- Device nodes: stock ROM sets 0660 system:system via odm `tee-supplicant.rc` / `vendor.xiaomi.hardware.mitrustedui.rc` plus SELinux types
  `mitee_client_device`/`mitee_tui_device`; on a custom ROM without those files the nodes stay 0600 root (ueventd default). `/proc/mitee_log_key` is 0444 (S16).
- Equivalence: msgq logic is faithful; fix A1 (and optionally A2) before any device test.
