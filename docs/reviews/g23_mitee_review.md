# Review G23c (buckchg batt_emerg) + mitee_dlkm G15b (GLOBAL OS4.0.0.7, read-only)

Reviewer: independent adversarial review, 20.10.2026. NOT a safety statement (charging is community-only; mitee_dlkm stays DO NOT LOAD).
Nothing was built, loaded, flashed or run on a phone; no fastboot/adb/git; sources not modified.
Method: own `llvm-objdump -dr` (clang-r510928) of the stock (`<work-dir>`, md5 buckchg feab3804..., mitee 431aa448...)
and of the already built `.ko` (`<work-dir>`, `<work-dir>`);
string-resolved, normalised listings and by-hand block pairing. Scratch `<work-dir>` (fx.sh, nf.sh, nfs.sh, cf.sh, rs.py; dumps only).
Every item is marked VERIFIED (how) or INFERRED.

## A. Charging: G23c fix in mca_strategy_buckchg (inlined select_charg_para)

| Check | Result |
|---|---|
| Offsets | stock `.text+0x2208 ldr w8,[x19,#0x5bc]; cbnz` / `0x2210 ldr w8,[x19,#0x6f0]; cbz` / `sub x8,x19,#0x228; ldr; cmp #0; csel 500/1000`. x19 = the work pointer (`mov x19,x0` is its only write; `x0-0x160` = quick_chg_status +208 => work = info+0x230). +0x5bc -> info+0x7ec = BTF `batt_missing` (+2028), +0x6f0 -> +0x920 = `batt_emerg` (+2336), -0x228 -> +8 = `batt_type`. VERIFIED (listing + BTF dump g14e). |
| Polarity | `batt_missing != 0 || batt_emerg != 0` -> `batt_type == 0 ? 500 : 1000`. Ours (G23c build) has the identical 10-insn sequence at its +0xbc8..0xbec; `before_buckchg.ko` has no load of +0x6f0. VERIFIED (listing). |
| Only reader | stock: one `str [x19,#0x920]` (process_batt_emerg_change) and one load (above); searched every immediate that equals 0x920 minus a work offset (monitor, sw_cv, base_flip, wls_revchg, csd, source_status, pd_secret, rerun, cp_i2c, stepper, otg) plus the notifier/callback functions: no other access. VERIFIED (grep of all bases). |
| Units | the value is passed unscaled to ADSP glink prop 0x2008 (below); mA INFERRED (DT in_sdp 500, in_dcp 1500, in_pd 1600 fit). |

Following the value (VERIFIED, stock listings): `mca_vote(target_volt==5000 ? buck_5v_in : buck_9v_in, "wire_chg_type", 1, v)` only when v != proc_data.ibus_limit
(+0x164) or the voltage changed, then `set_restart_aicl(MAIN,1)` (subpmic glink 0x2014) -> set_5v/9v_input stores +0x130/+0x138 (never read)
and votes `buck_input` "volt_limit" -> `strategy_buckchg_input_limit` -> `platform_class_buckchg_ops_set_input_curr_lmt(0,v)` -> ops+0xd0 =
`qcom_subpmic_set_input_current_limit` (mca_qcom_subpmic_proxy g_qcom_buckchg_ops, reloc .data+0x1f8) -> `mca_adsp_glink_write_prop(0x2008,&v,4)`.
dada global dtbo (an/dtbo/g.dts:3429): buckchg node has no `batt_type` -> default 0 (probe `mov w3,wzr`) -> 500; `support_multi_buck = 0`.
mca_vote (mca_common 0x5f4): callback only if the effective value or client changed and no override is active.
Other consumers: mca_charger_thermal only votes on buck_5v_in/9v_in; fg_comp tests buck_input's effective result only `!= 0`; quickchg holds
overrides "icl_limit"/"pmic_temp_term" on buck_input (which mask all normal votes while active). VERIFIED (listings), quickchg x19 = info INFERRED.

Effect on dada after event 85 (VERIFIED from code, not from hardware): buck_input (MIN) already holds "batt_emerg" 500, so the fix changes the
effective input limit only where the per-type value was below 500: UNKNOWN type 100 (jump-table entry 0 -> straight to the test with w24 = 100)
and EU-model SDP 450 are raised to 500 (stock); all others stay 500; plus one restart-AICL request. Stock-only nuance kept in both: in the first
60 s of boottime with smem zero-speed, input_limit raises any non-zero result < 1500 to 1500 (so the boot-time event 85 can be 1500 then).
`batt_emerg` is never cleared: the override lasts until reboot. Verdict: G23c fix CORRECT (equal to stock), no other change needed.

batt_emerg_chain_review.md corrected in place (marked "CORRECTION (review-G23)"): verdict (a second difference existed: D7), the buckchg
consumer line (full value path), "What DOES act" (wire_chg_type 500, raised UNKNOWN/EU-SDP, 60 s zero-speed 1500, overrides, flag never
cleared), vote-lifetime table (new row), units (csd 1500 is on buck_charge_curr only, buck_input goes unscaled to glink 0x2008), table (6)
(new D7; "buckchg MATCHES" was wrong for the reviewed snapshot).

## B1. mitee_dlkm: the four G15b BEHAVIOUR fixes

| Fix | Stock | Ours (g15b build) | Verdict |
|---|---|---|---|
| core.c optee_to_msg_param | registered memref (`shm && flags & BIT(3)` at shm+0x3c) -> `comm_ops(+0x18)->to_msg_param(+0x30)` with no id load; `mitee_msgq_com_ops` id word in stock .rodata+0x29a0 = 1 (QCOM_QMI) | identical | CORRECT. Normalised listing incl. resolved strings = stock (VERIFIED). No other `comm_ops->id` test is left in the sources (grep). optee_from_msg_param also identical (only the jump-table relocation name). |
| mitee_msgq_rpc_callback | `ldr w9,[x0]` / `ldr w8,[x0,#8]` (low 32 bits), FREE: `g_msgq_dev` loaded only there, 64-bit handle compare, reclaim((u32)h) error only printed, assign {peer}->{this} perm 7, kfree, free_sgt, 0; ALLOC: ALIGN(u32 size), lend, value->a = handle, value->b = (u32)size; error 0xffff0006 | same blocks; b re-loaded per branch; vmid/perm arrays in other stack slots (contents equal: src [0xaa]=peer, dst [0xa8]=this, perm 7) | CORRECT; rest CODEGEN (VERIFIED by pairing). |
| mitee_shm_rem_mem_handle | no kfree after a failed rhashtable_remove_fast | identical (local call name only) | CORRECT (VERIFIED, normalised listing). |
| tee_shm_register | no ctx NULL test | identical (local call name only) | CORRECT (VERIFIED). |

## B2. Deliberate "safer than stock" deviations

| Deviation | Stock (VERIFIED, listing) | Valid input changed? | TEE protocol changed? |
|---|---|---|---|
| tui_shm_alloc ERR_PTR | vmalloc_user / idr_alloc failure -> kfree, `mov x19,xzr` (NULL); caller tui_ioctl_tui_shm_alloc tests only IS_ERR then reads shm+0x14 -> NULL oops | NO (only OOM / id exhaustion) | NO (user ioctl path) |
| tui_shm_create_fd unlock | anon_inode_getfd < 0 -> return -12 with tuiMutex held (also the flags-invalid path, which ours keeps leaking like stock) | NO (fd-table / OOM failure only) | NO |
| shm_register ACL kmalloc | ACL kmalloc(12) happens BEFORE the forward hyp_assign; on failure stock still does reclaim(handle 0) + reverse hyp_assign {this,peer}->{this} on pages never assigned | NO (kmalloc(12) failure only) | NO msgq message; it drops one useless RM reclaim(0) and one SCM assign that must fail or no-op (INFERRED) |
| memlog_read -EFAULT | BOTH rings (runtime and boot; STATUS names only runtime): `subs put,get; b.hs ok; mvn ~get; cmp; b.hs ok` is always true when put < get, so -EFAULT is unreachable; the stock goes on with outbuf = min((int)(put-get), size) | **YES**: get/put/alloc are free-running u32 counters (indexes masked with sz-1 in the copy loop, VERIFIED), so after 2^32 bytes of TEE log the counters wrap; stock then reads the correct (put-get) mod 2^32 bytes, ours returns -EFAULT once and drops the unread data (s->get = put). Reachability (4 GiB of log since boot) INFERRED low | NO (only s->get/b_get in the driver state are written, never the shared log header; VERIFIED stores go to the state base, not log) |

## B3. Remaining differing functions (block pairing)

| Function | Class | Evidence |
|---|---|---|
| mitee_recv_task_fn | CODEGEN | All 6 type branches paired: ctrl ids>>16 == 0x8000, msg_id 2 (len warn, state 3), 1 (len warn then the two 0x50 sends: {5, pa, 0x200000, shm_handle, shm_label} and {12, task_buf_pa, size, msg_handle, msg_label}, udelay 5000 = 0x147aeb8, share_mem_contiguous between), !0 pr_info, 0 (state >= 2 dev_err, len check, ids|0x8000, send 0x28, state 2); type 1 warn; 4 ring push (rx bound: stock `cmp #0x21; b.hs`, ours `cmp #0x20; b.hi` = the same predicate head >= 33); 5 mailbox; default print. Byte table [0,139,146,146,25,56] vs ours follows only the block order of the 2/1/!0/0 chain; ours keeps `ids` in w23 instead of reloading after the printk (same value). |
| mitee_memlog_read | CODEGEN + the B2 deviation | Paired line copy (mask sz-1, +0x3cb, 255 cap with the ubsan bound written as count-1 > 254 = count > 255), break path, copy_to_user (local `.text+0x3208` instance vs `.text._copy_to_user`), poll_event/readable epilogue. Boot ring: stock reloads log->sz inside the byte loop, ours hoists it (sz is a constant header field; INFERRED harmless). |
| mitee_memlog_probe | CODEGEN + 1 LOG-TEXT difference | Stack slots swapped, two redundant zero stores of md_region name[8..12]; same region content {"MITEELOG", id 0, va, pa, size}. NEW: the minidump-failure printk gets `__func__` = "add_log_memory_to_dump_region" twice in the stock (real inlined helper), ours prints "mitee_memlog_probe: add_log_memory_to_dump_region: ...". Log text only. |
| mitee_msgq_shm_register | CODEGEN + the B2 ACL deviation | supp check -13, check_mem_type, sgt kmalloc(16), sg_alloc_table(UINT_MAX seg), ACL {2, this/6, peer/6}, forward assign {this}->{this,peer} perm {6,6} failure only logged, sgl NULL -> -12 with handle 0, mem_share failure -> handle 0, entry kmalloc(32) failure -> "add handle failed: -12", inline rhashtable insert, -EEXIST/-E2BIG, sec_world_id = (u64)handle; every error path reclaim(handle) + reverse assign {this,peer}->{this} perm 7 + free: equal. |
| mitee_msgq_secmem_lend | CODEGEN | ACL {1, peer/7}; forward {this}->{peer} perm 7; every failure -> reverse {peer}->{this} + mitee_free_memory_sgt; lend failure handle 0; sm kmalloc(40) failure -> reclaim(handle) and return 0 (stock quirk, kept); list_add_tail under secmem_mut; *out = (u64)handle. Ours keeps the handle in its stack slot. |
| mitee_msgq_shm_unregister / _supp | CODEGEN | msg_arg {cmd 5, 1 param, attr 5 RMEM_INPUT, shm_ref}, lookup sgt, rem_mem_handle, sec_world_id = 0, do_call_with_arg (error printed), return = reclaim rc, reverse assign {this,peer}->{this} perm 7, sg_free_table, kfree: equal; handle in [sp] vs x22. |
| (incidental) mitee_msgq_to_msg_param, tui_shm_alloc | CODEGEN / the B2 ERR_PTR | address arithmetic only / ERR_PTR codes only. |

Stock quirks kept bug-compatible (relevant for any load decision, all only reachable by a misbehaving TEE or OOM): rpc_callback FREE with an
unknown handle frees `qdev+0x1b8` (list-head container); shm_unregister(_supp) with an unknown handle passes sgt NULL to hyp_assign_table /
sg_free_table; secmem_lend reports success (handle 0) after freeing the memory when the 40-byte kmalloc fails.

## Fix proposals (BEHAVIOUR findings)

1. memlog_read (valid-input difference at the u32 wrap): delete both `if (get > put) { s->(b_)get = put; return -EFAULT; }` blocks in
   `do_mitee_memlog_read` (mitee_memlog.c ~190 and ~244). That is exactly the stock behaviour (its check is a no-op); `min((int)(put - get), size)`
   already handles the wrap. Then rebuild and re-pair; update STATUS G15b (both rings; "safer" was not true at the wrap).
2. memlog_probe log text: move the md_region block into `static inline void add_log_memory_to_dump_region(void *va, phys_addr_t pa, size_t size)`
   with `pr_err("%s: Failed to add %s data in Minidump, add region failed!\n", __func__, "MITEELOG")` (same string, stock `__func__`).
3. Keep the three no-valid-input deviations (tui_shm_alloc, tui_shm_create_fd, shm_register ACL) and say in STATUS that they change only
   OOM/fd-exhaustion paths and no msgq message.

## Ordered checklist before mitee_dlkm could leave DO NOT LOAD

1. Apply fix 1 (memlog_read) and, optionally, fix 2; rebuild; normalised compare of those two functions.
2. Pair by hand the byte-differing functions NOT covered here (mitee_worker, optee_do_call_with_arg, mitee_msgq_round_trip_call, tee_ioctl,
   optee_open_session, init/mitee_tee_init, msg_queue_register, sgt_to_gh_sgl_desc, tui_process_tuisvc_callback, send_cmd_to_user): G23c showed
   that a symbolic CLEAN/CLASS-A verdict can hide a dropped condition.
3. Build against the real target kernel (its headers decide task_struct.mm, copy_*_user, poll_wait) and the real Module.symvers of
   gh_rm_drv / gh_msgq / secure_buffer / qcom-scm / minidump; drop the forged os4_* providers and forged depends=; check vermagic + CRCs.
4. Decide and write down the bug-compatible stock quirks policy (keep for equivalence vs harden) before any load.
5. Only with the user's explicit approval and a full backup, stock mitee_dlkm kept as fallback: one boot test, check dmesg ("recv task start",
   vm_state 3, no "state error"), /proc/mitee_log readable, one TA session (wire format is still INFERRED from this module only).

Not done here: hardware; ADSP-side meaning of glink 0x2008; the functions in checklist item 2; mca_common override release path.
