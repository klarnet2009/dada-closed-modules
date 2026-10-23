# minet - EQUIVALENT (G57a: 44 of 44 function bytes, 730 relocations, kCFI preambles 41/41, objects, imports+CRC, modinfo identical to the GLOBAL OS4.0.0.7 stock; compare_ko_v2 fails only bug-lines (header line numbers) and buildmeta; not hardware tested, DO NOT LOAD)

## G57a (22.10.2026) - the 2 "ThinLTO-promoted hidden functions with a kCFI word" were SOURCE-REACHABLE (md5 6b77b5e58c1a5c29907c41acc000f650)
Build: real Kleaf (scratch <work-dir> tag g57a, bazel-out removed) vs the GLOBAL OS4.0.0.7 stock; tools work/re/src/g57a_tools (nd57 = raw `llvm-objdump -dr` diff with relocation targets resolved to symbol/string content, wl57 = register-allocation independent dataflow labels, alu57/sym57/str57 = non-register skeleton, stset57 = store multiset). Evidence per function: work\a17\review\g57a\<module>__<function>.txt, compare_ko_v2 logs <module>__compare_ko_v2.txt.
Evidence (raw): the stock `reset_timer.llvm.8540474682487888066` and `clear_tc_app.llvm.15945034295104651021` are FUNC GLOBAL HIDDEN, the 4 bytes before them are the last `bl` of the previous function (no kCFI type word) and every reference is an R_AARCH64_CALL26 (never address-taken). clang's `finalizeKCFITypes` removes the kcfi_type metadata of local-linkage functions that are not address-taken, ThinLTO then promotes them (name + `.llvm.<hash of the TU bitcode>`). So in the original source both are `static`. The earlier hack (non-static function with an `__asm__("name.llvm.hash")` label) made them external, hence the preamble.
Fix: `static void reset_timer(...)` (minet_idletimer.c), `static void clear_tc_app(void)` and `static mi_idletimer_tg_info m_idletimer_info` (minet_tc.c), all three asm-label hacks removed. The stock calls reset_timer directly from tl_after_filter_in / minet_set_tc_enable because ThinLTO imported the 48-byte exported `mi_idletimer_tg_target_v1(int *puid, struct mi_idletimer_tg *timer)` into those functions: the source of the TC code now calls `mi_idletimer_tg_target_v1((int *)&uid, m_idletimer_info.timer)` / `(NULL, ...)` instead of `reset_timer(timer, timer->timeout, ...)` (identical code after inlining). Experiments (scratch minetx, not kept): static clear_tc_app only -> clear_tc_app preamble gone, reset_timer remained; + target_v1 + static reset_timer -> 44/44 bytes, 730 relocs, 41/41 kCFI.
Result vs GLOBAL: PASS function-set 44, function-bytes 44/44, relocs, binding, kcfi-preamble 41 (was FAIL 2 of 43), objects, anon-data, bug-table, other-tables, exports, imports 60 + CRC, modinfo; FAIL only bug-lines (`include/net/netlink.h` line 1037 stock vs 1048 with the 6.6.144 headers: not reachable) and buildmeta. The `.llvm.<hash>` suffixes are now the natural ThinLTO ones (reset_timer.llvm.8692045186260629678, clear_tc_app/m_idletimer_info.llvm.10349864300743148719 instead of 8540474682487888066 / 15945034295104651021): the suffix is a SHA of the TU bitcode and cannot be forced; compare_ko_v2 strips it, kallsyms names differ in the digits only.

(previous first line before G57a: minet - PARTIAL (strict compare_ko_v2: data fixed; remaining REAL difference: 2 ThinLTO-promoted hidden functions reset_timer/clear_tc_app carry a kCFI preamble word that the stock does not have))

## G20a (20.10.2026) - kCFI audit + imports/CRC vs GLOBAL OS4.0.0.7 (first line unchanged)
- `clear_tc_app.llvm` / `m_idletimer_info.llvm` hash changed to the GLOBAL stock value (15945034295104651021; CN had 6912468423217759055; `reset_timer.llvm.8540474682487888066` was equal). Rebuilt (g20a): function-bytes 44/44 identical, relocs 730 identical, imports 60 + CRC PASS. kCFI: still 2 of 43 preambles differ (reset_timer, clear_tc_app: stock has NO kCFI preamble, ours has one; `no_sanitize("kcfi")` does not remove it, the functions are ThinLTO-promoted statics in the stock and non-static with an asm label here; never called indirectly, harmless; not fixable without a static/promoted emulation).

## G17a (19.10.2026) - global OS4.0.0.7 re-measurement and function classification (first line unchanged unless noted: still PARTIAL, no hardware test)
Global OS4.0.0.7 stock (<work-dir>) vs CN OS4.0.0.6 stock: code-identical (cn_vs_gl.csv, compare_ko_v2 stock-vs-stock: STRICT-EQUIVALENT). Real Kleaf build (scratch <work-dir> tag g17a, bazel-out removed) vs the GLOBAL stock: function set 44 OK, ALL 44 function bodies and 730 relocations identical; only kcfi-preamble (the 2 ThinLTO-promoted hidden functions reset_timer/clear_tc_app) and bug-lines (line numbers) fail = linking/metadata artefacts, not behaviour. 0 BEHAVIOUR differences, no source change.
Tools: <work-dir> (kb17.sh = Kleaf build + compare_ko_v2 + preamble_cmp, sym17.sh = symcmp4 on every differing function, str17.py = printk format scan, fd17.sh = disassembly diff), results in res/<module>.cmp / .sym. Hacks: none added; earlier hacks (forged CRC / stubs / codegen hints) unchanged and still listed above.


Old first line (before the strict recheck G9): # minet - EQUIVALENT (44 of 44 functions identical to the OS4 stock, compare_ko.py EQUIVALENT; ; hacks: ThinLTO-promoted names as hidden globals, `(unsigned long)buf == 0` codegen hint, local declaration order)

## Update (17.10.2026)
- `minet_set_tc_enable` now identical: the stores to `m_tc_enable` are `WRITE_ONCE` (volatile store keeps the `adrp` of `m_idletimer_info.timer` after the store).
- `minet_dump_tc_list` rebuilt (BTF/disassembly analysis, 21 masked words left, was 204): the stock keeps `room = (size_t)cnt*5` and `size = room + 8` as 64-bit values, `status = 2` is stored right before `read_lock_bh`, kzalloc + `*(u32*)buf = 1; buf[1] = cnt; p = buf + 8; status = 3`,
  the `m_tc_list.cnt` print is shared by the OK and the failure path, the loop is `if (!list_empty()) list_for_each_entry_SAFE(...)` with `if (p && room >= 4) {*(u32*)p = uid; p += 4; room -= 4;} if (p && room) {*p++ = type; room--;}` (the bound checks `brk #1` of the stock are the compiler's local-bounds checks on the `alloc_size` buffer, no explicit `off`),
  reply order: config (log only on failure), then applist only if `buf && room == 0` (a failure only logs, no early return), then status; one common tail `dumping = false; if (buf) kfree(buf)`. The remaining 21 words are the block layout of the kzalloc failure path (stock: failure block inline right after the `cbnz`, then `b join`, then the success block; ours: failure block out of line)
  and the order of `mov w21,wzr`/`mov x24,xzr`; tried `if (buf)`/`if (!buf)`/goto forms, `__builtin_expect(_with_probability)` at 0.5..0.99: none gives the stock layout (a probability > 0.5 gives fail-inline but without the `b`).
- `mi_idletimer_tg_checkentry_v1` still 22 words (x22/x23 swapped). The MIR before greedy RA (`ld.lld -r -mllvm -print-before=greedy -mllvm -filter-print-funcs=...`) shows the same CFG as the stock; the order is decided by the allocator priority of `&idletimer_tg_list` (%37) vs `&info->timer` (%137). Tried: separate create function, open-coded find loop: both worse. Not solved.
- Tools: `work/re/src/g3d_tools/` (copy of g3c_tools with server dir `<work-dir>`).


## Update (17.10.2026): the 21 missing functions were reconstructed
Build: `bash work/re/src/g3c_tools/g3_build.sh minet` (real Kleaf/ThinLTO + compare_ko): function set 44 OK, relocations OK, objects OK, imports (60) + CRCs OK, exports 0, modinfo OK.
Still different (same instruction set, scheduling/register-allocation level only): `minet_set_tc_enable` (6 words: placement of one `adrp`), `mi_idletimer_tg_checkentry_v1` (x22/x23 swapped: which of `&idletimer_tg_list` / `&info->timer` gets which callee-saved register),
`minet_dump_tc_list` (structure of the app-list builder: stock keeps `cnt*5` as a 64-bit value, sets status=2 before the kmalloc, guards the loop with `list_empty`, bounds-checked cursor; our form is functionally the same but compiles differently). Next step: try statement-order variants of these three.
Source layout (new): `minet_idletimer.c` (in-module IDLETIMER copy: `mi_idletimer_tg_{checkentry,target,destroy}_v1`, `reset_timer`, `idletimer_tg_{work,expired}`, `idletimer_resume`, `print_idletimer_info_safe`), `minet_tc.c` (TC limiter: `minet_tc_limiter_init/finit`, `tl_after_filter_in`,
`add_tc_app`, `del_tc_app`, `clear_tc_app`, `minet_set_tc_enable/_list`, `minet_dump_tc_list`), `minet_tc.h` (structs), `minet_nl.c` (+ `minet_nl_reply`, `minet_idletimer_notify`, 4 genl ops cmd 1/3/5/6, MINET_ATTR_MAX 4, replies cmd 7), `minet_core.c` (`tl_after_filter_in` in hook4/hook6 for NF_INET_LOCAL_IN, limiter init/finit).
Recovered from the disassembly + the stock BTF (`START_ID=141631 btf_dump_startid.py local minet.ko`): `struct mi_idletimer_tg` (376 bytes, the ACK `struct idletimer_tg` with a `char name[32]` instead of the sysfs attribute; offsets verified by `_Static_assert`), `mi_idletimer_tg_info` (80 bytes: timeout, label[28], send_nl_msg, timer_type, iface[16], mask[16], timer*),
the netlink packet of `idletimer_tg_work` (48 bytes: label[28], state, time_ns, uid, sent with cmd 4/attr 1), the TC config reply (92 bytes), `m_tc_list` {cnt, rwlock, list}, `struct tc_app` {u8 type, u32 uid_aid, list}.
Template for the idletimer part: ACK `net/netfilter/xt_IDLETIMER.c` (`work/srcsurvey/micode_dada`, v1 only; `reset_timer(timer, timeout, int *puid)`, no sysfs, no timer_type/alarm, `send_nl_msg` fixed to true, `timer->timeout` used on re-arm).
Documented hacks: ThinLTO-promoted names defined as hidden globals (`reset_timer.llvm.8540474682487888066` declared with the asm label in `minet_tc.h`, `clear_tc_app.llvm.6912468423217759055`, `m_idletimer_info.llvm.6912468423217759055`). The reply/dump payload layouts are inferred from code (not checked by compare_ko).

---- earlier notes ----

Reference binary: `<work-dir>` (vermagic 6.6.57-android15-8; own BTF base, `START_ID=141631` for btf_dump).
Template: XRING tree `work/re/xiaomi-modules/mi_net/minet/` (minet_core.c, minet_nl.c, minet_nl.h, minet_debug.h), used unchanged (copied here by the main session; BUILD.bazel + ThinLTO defconfig exist).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, compare_ko.py):
- (a) COMPILES.
- (b) function set DIFFERS: stock 44 functions, rebuilt 23. Of the 23 built functions 18 are byte-identical; relocations identical; exports (0) OK; modinfo OK.
- Different bytes: `hook4`, `hook6`, `minet_nl_cmd_set`, `init_module`, `cleanup_module` (they reference the missing new code: tc limiter hooks, idletimer, extra netlink commands).
- 27 of the 60 stock imports are missing in the rebuilt module because the new code is missing (mod_timer, timer_delete, init_timer_key, queue_work_on, cancel_work_sync, register/unregister_pm_notifier, set_normalized_timespec64, ns_to_timespec64, ktime_get_with_offset, _raw_read/write_lock_bh, mutex_lock/unlock, ...).

What is missing (21 functions; no template in any tree we have):
- TC limiter: `minet_tc_limiter_init/finit`, `tl_after_filter_in`, `add_tc_app`, `clear_tc_app.llvm.<hash>`, `minet_set_tc_enable`, `minet_set_tc_list`, `minet_dump_tc_list`, `minet_nl_tc_enable_set/_list_set/_list_dump`, `minet_nl_reply`, global `m_tc_enable`. New Xiaomi code, no public template.
- In-module copy of the netfilter IDLETIMER target: `mi_idletimer_tg_checkentry_v1/_target_v1/_destroy_v1`, `idletimer_tg_expired/_work`, `idletimer_resume`, `reset_timer.llvm.<hash>`, `print_idletimer_info_safe`, `minet_idletimer_notify`, object `m_idletimer_info.llvm.<hash>` (80 bytes). Derived from kernel `net/netfilter/xt_IDLETIMER.c` (a MiCode copy is in `work/srcsurvey/micode_*`).
Next step if someone continues: reconstruct these 21 functions from `<work-dir>` (stock disassembly), then re-diff hook4/hook6/nl_cmd_set/init/cleanup.
Build: `bash work/re/src/a17_push.sh minet` then `<work-dir> minet` on the server.

## Update (17.10.2026, no code change, analysis only)
Read the stock disassembly of the idletimer copy (`<work-dir>`, compact view: `python3 <work-dir> minet <func>`). Findings for whoever continues:
- It is the Android-common-kernel `xt_IDLETIMER.c` (struct `idletimer_tg`: list@0, timer@0x90, work@0xc8, label@0xf8, pm_nb@0x148, timeout@0x160, refcnt@0x164, send_nl_msg@0x168, active@0x169, timer_type@0x16a, work_pending@0x16b, uid@0x16c; kmalloc size 0x178)
  but with the sysfs part removed and the uevent replaced by the Xiaomi netlink notifier: `idletimer_tg_work` builds `INTERFACE=%s` ("wlan0" literal), `STATE=%s`, `UID=%u`, `TIME_NS=%llu` strings (error texts `minet-e: iface_msg/state_msg/uid_msg/timestamp_msg too long (%d)`)
  and calls `minet_nl_msg(msg, 0x30, 0, 4, 1)`; `minet_idletimer_notify` is a 4-instruction wrapper of `minet_nl_msg(.., 0, 4, 1)`. `idletimer_resume` is the PM notifier (PM_SUSPEND_PREPARE=3 / POST_SUSPEND=4, `set_normalized_timespec64`, `mod_timer_pending`).
- The `mi_idletimer_tg_info` argument of `mi_idletimer_tg_checkentry_v1` has {u32 timeout@0, char label[28]@4, u8 send_nl_msg@0x20 (<2), u8 timer_type@0x21, ...}; `print_idletimer_info_safe` dumps it as `"%u %s %u %u %s {0x%02x x16}"` (`minet-i: dump info{%s} size:%d len:%d`, level 6) and
  shows two more members (char[16] @0x22, u8[16] @0x32), so there is no public template for the Xiaomi struct; `reset_timer.llvm.8540474682487888066` is the ThinLTO-promoted static `reset_timer`.
- TC limiter (12 functions, ~3.7 KB) not looked at beyond the symbol sizes. Estimated cost for both parts: 100k+ tokens; not done.


## Update (17.10.2026) - SOLVED, EQUIVALENT
Real Kleaf/ThinLTO build + compare_ko.py (functions 44, bytes, relocs, objects, imports 60, exports 0, modinfo all OK).
- `mi_idletimer_tg_checkentry_v1` (was 22 words, x22/x23 swapped): the real cause was a SOURCE difference, not the allocator: the stock arms the timer with `mod_timer(&info->timer->timer, msecs_to_jiffies(info->timer->timeout * 1000) + jiffies)`
  (reload of `timer->timeout` after the calls, as in the ACK xt_IDLETIMER), we used `info->timeout`; the stock therefore keeps `&info->timer` live to the end of the function (longer live range -> allocated first -> x22).
- `minet_dump_tc_list` (was 21 words): (a) the null test is written `if ((unsigned long)buf == 0)` (codegen hint: an integer compare is not treated as an unlikely pointer-null branch by BranchProbabilityInfo, so the failure block stays inline like the stock;
  `!buf`, `unlikely`, `__builtin_expect_with_probability`, `__builtin_unpredictable` did not give the layout); (b) the local declaration order `u32 status; u8 *buf; u8 *p; size_t room; size_t size;` (phi order in the join block = alloca order decides the order of the `mov w21,wzr`/`mov x24,xzr` copies).
Documented hacks: hidden `.llvm.<hash>` globals (see below), the integer-compare null test and the declaration order (codegen hints, behaviour unchanged). The reply/dump payload layouts remain inferred from code.
Tools: `work/re/src/g3f_tools` (`mn.sh` quick loop, `varsearch.py` with env STOCK=minet).

## G9 strict recheck (18.10.2026)
compare_ko_v2 found (a) 5 variables in stock `.data` with non-zero values (m_tc_mask=0x30000000, m_white_mark=0x10000000, m_black_mark=0x20000000, m_user_offset=100000, m_app_start=10000; read with `objdump -s -j .data` at offsets 0x1e0..0x1f0): FIXED with initialisers in minet_tc.c, objects PASS now.
(b) kCFI preamble on `reset_timer.llvm.*` / `clear_tc_app.llvm.*`: STILL OPEN. In the stock these are static functions (no preamble) that ThinLTO promoted to GLOBAL HIDDEN; ours are real hidden globals (hash-name hack), clang emits a type-hash word before them even with no_sanitize("kcfi") (the attribute only drops the checks). Tried (3 builds): `static` + `asm("name")` + module asm `.globl/.hidden` -> preamble gone for clear_tc_app, binding GLOBAL HIDDEN, BUT module asm blocks ThinLTO import/inlining: minet_tc_limiter_finit is no longer inlined into cleanup_module (function-bytes and relocs of cleanup_module differ). Reverted to the previous source (old compare_ko EQUIVALENT, 44/44 functions byte-identical, kCFI preamble_cmp 0 mismatches for the non-promoted functions).
Other compare_ko_v2 differences (acceptable): buildmeta (srcversion/built_with), bug-lines (minet_nl_msg/minet_nl_reply `__LINE__` 1037 vs 1048, cosmetic).
Idea not tried: a per-function way to suppress the type hash (a clang/LLVM patch or post-link strip of the 4 bytes), or restructure so ThinLTO itself promotes the statics (name hash then differs from stock).
