# process_monitor - PARTIAL (38 of 42 functions byte-identical; the other 4 have block-level EQUIVALENT-PROVEN verdicts in G55a), NOT hardware-tested

## G27b (20.10.2026) - detect_list_write condition confirmed equal (first line unchanged; no source change)
- G25a same-atoms POLARITY-DIFF of detect_list_write: stock `cmp x23,#0x64; b.hi trap` vs ours `cmp x23,#0x65; b.hs trap` (same target, same unsigned 64-bit operand x23 = clamped length): `x > 100` and `x >= 101` are the same condition for integers. CODEGEN. The other length checks of the function (`cmp x0,#0x65; b.hs` after the strnlen-style calls) are identical on both sides.

## G17a (19.10.2026) - global OS4.0.0.7 re-measurement and function classification (first line unchanged unless noted: still PARTIAL, no hardware test)
Global OS4.0.0.7 stock (<work-dir>) vs CN OS4.0.0.6 stock: code-identical (cn_vs_gl.csv, compare_ko_v2 stock-vs-stock: STRICT-EQUIVALENT). Real Kleaf build (scratch <work-dir> tag g17a, bazel-out removed) vs the GLOBAL stock: function set 42 OK, objects/imports/modinfo/kCFI OK (failing only function-bytes and relocs); 38 of 42 byte-identical; init_D_task, init_signal_record, signal_record_hook_handle = symcmp4 CLASS-A-auto; detect_list_write = CODEGEN (stock cmp x23,#0x64 b.hi vs ours cmp #0x65 b.hs, csel forms of min(512-len,0); same condition). 0 BEHAVIOUR differences, no source change.
Tools: <work-dir> (kb17.sh = Kleaf build + compare_ko_v2 + preamble_cmp, sym17.sh = symcmp4 on every differing function, str17.py = printk format scan, fd17.sh = disassembly diff), results in res/<module>.cmp / .sym. Hacks: none added; earlier hacks (forged CRC / stubs / codegen hints) unchanged and still listed above.


## G1ai (18.10.2026) - timeout_check kCFI hash 0x6a311a23 SOLVED (first line unchanged: 38 of 42 byte-identical)
- The element type of the D_task list is **`task_node_t`**: `_ZTSFvP11task_structP11task_node_tE` hashes (xxh64 low 32 bit) to exactly 0x6a311a23. Found by a dictionary brute force (g1ai_tools/kcs3.c: 1-2 words of the 19064 most common identifier tokens of the 929 OS4 .ko files, 5 spellings, optional `_t`, ret void, `P`/`PK`; 3 hits among 5.8e9 candidates, the other two are gibberish `xfi_Krf`, `xyj_Tqtx_t`). The name is made of real words and makes the real kCFI word identical, so it is taken as the real name. Not proven whether the original is a struct tag or a typedef: `typedef struct { char task_name[16], node_name[16]; int timeout, state; bool flag; } task_node_t;` (anonymous struct + typedef, 44 bytes) gives the same hash in Kleaf (clang C mangles a typedef-named anonymous struct by the typedef name), so that form is used.
- Earlier searches failed because the invented tag names (dtask/item/node/info...) did not contain the word `task_node` + `_t`; anonymous-struct mangling (`Ut_`..`Ut400_`) and every builtin return type were tried too (no hit).
- Real Kleaf + compare_ko_v2 + preamble_cmp after the change: kcfi-preamble PASS 41 of 41 (was: timeout_check open), function set 42, binding, objects (19), anon-data, bug-table + lines, jump-table, other-tables, exports, imports (70)+CRC, modinfo, buildmeta, depends, vermagic, notes all PASS; only function-bytes/relocs: the same 4 functions as before (signal_record_hook_handle, detect_list_write, init_D_task, init_signal_record). The rename `struct dtask_item` -> `task_node_t` touches only d_task.c (5 lines).

## G1ah (18.10.2026) - timeout_check kCFI hash 0x6a311a23 was OPEN (SOLVED in G1ai, see above)
- The word at stock 0x13e4 is a real type id (the previous function ends with `b` at 0x13e0). Params from the disassembly: x0 = struct task_struct * (nvcsw at +0x748), x1 = item (timeout at +0x20). Brute force `_ZTSF<ret><params>E` (xxh64 low 32 bit, kcs2 with Itanium substitutions): ret in {void,bool,int,unsigned,long,...}, arg1 in {task_struct*, const task_struct*, void*, char*}, arg2 = pointer / const pointer to a struct with ~4.1 million invented tag names (1-3 word combinations of dtask/d_task/Dtask/task/item/node/info/entry/list/detect/ignore/monitor/proc/data/rec/..., lower/UPPER/Capitalized, `_` or camel, plus all 10680 pointer types of the base BTF) and also 3-parameter variants (third = base type): NO hit. Strings of the stock suggest the Dtask struct names `Dtask_struct` (functions `Dtask_struct_init/free`), tried in all forms. Next: read the tag from the call chain (check_task passes `&g_pDtask->list[i]`, so the item type is the element type of `list` in the struct that `Dtask_struct_init` allocates: size of the element from the kmalloc size / stride in check_task), or the type may be a typedef/anonymous struct (then the id uses the typedef name or a lambda-style name).


g4j (17.10.2026): real Kleaf build re-run: function set (42), relocations, objects, imports (70)+CRCs, modinfo OK; 38 of 42 byte-identical, differing: signal_record_hook_handle, detect_list_write, init_D_task, init_signal_record. No function is missing. Not touched in g4j (budget went to lb).

g4d (17.10.2026): real Kleaf build re-run: function set (42), relocations, objects, imports (70) + CRCs, modinfo OK; 38 of 42 identical. Tried 7 more orderings of the `h->cur_pos++ / h->total++` updates in `signal_record_hook_handle` (real builds): all worse (diff 132 lines vs 19), so the original order stays.


Re-checked against `<work-dir>` (real Kleaf ThinLTO build of the unchanged source): the OS4 binary matches in function set (42), relocations, objects, imports (70) + CRCs, modinfo;
the same 4 functions differ as in HyperOS 3 (`signal_record_hook_handle`, `detect_list_write`, `init_D_task`, `init_signal_record`). No OS4 specific change.

## Earlier rounds
`compare_ko.py` (ThinLTO build, `process_monitor_defconfig`): function set (42) OK, relocations OK, global objects OK, exports (none) OK, imports + CRCs (70) OK, modinfo OK.
kCFI type hashes (`perfB_fdiff.py -k`, not covered by compare_ko): all identical except `timeout_check` (second parameter is a pointer to a struct whose tag name is unknown).

## What still differs (function bytes)

- `signal_record_hook_handle`: only register allocation of the `cur_pos/total/max` update (`ldp w10,w9` vs `ldp w9,w10`; 8 diff lines). Source forms tried: ++/+=/local copies, order of the two updates.
- `detect_list_write` (17 diff lines): the order of the remaining-size computation around the `strchr` arms of the line splitter, and one fortify compare encoded
  `cmp x23,#0x64; b.hi` (stock) vs `cmp x23,#0x65; b.hs`. The `set_config()` parser is inlined (its `__func__` is "set_config", log lines 128..174).
- `init_D_task` (49 diff lines): stock keeps `mov w19,#-0xc` separately before the jump to the shared kfree tail on the mkdir/sub-proc failure paths and places the
  hook-registration failure block before it; ours merges the two `-ENOMEM` paths. Tried explicit duplicated cleanup, `ret` assigned before the log, nested `if (!ret)` hook registration.
- `init_signal_record` (44 diff lines): scheduling of the inlined `sig_driver_init()`/`sig_bitmap_init()` (g_sig_driver address materialisation order).

## Recovered source (three translation units like the stock build: `pm_main.c`, `d_task.c`, `sig_record.c`)

- `d_task.c` = D-state (hung task) detector: vendor hooks `android_vh_check_uninterruptible_tasks[_dn]`, `/proc/process_monitor/D_task/{detect_list,ignore_list,get_log,debug_into_D}`,
  netlink (unit 23, nlmsg_flags 0x4449 handshake carries the user pid) log upload, `crash_now()` BUG() when a listed task stays in D state for the whole timeout.
  `struct Dtask` (0x728 bytes): enable, num, ignore_num, two 20-entry tables of 44-byte items, log buffer, netlink sock, user pid, kthread, debug flag.
  Quirks reproduced: `kzalloc(512, count)` (count passed as gfp flags), MATCH log prints `killer->tgid` twice in `signal_record_hook_handle`, `ignore_list` is a stub.
- `sig_record.c` = signal history (vendor hook `android_vh_do_send_sig_info`), `/proc/process_monitor/signal_record/{history,monitor_list}`, 50-entry ring of 72-byte records, time is UTC+8.
- kCFI hashes give back struct/enum tag names: brute force `_ZTSF<ret><params>E` with xxHash64 (low 32 bits) - found `signal_record`, `signal_history`, `sig_driver`, `detect_result` (enum), `detect_list_op` (enum),
  `bool(int)`, `bool(void)`, `int(char*,int)`; tools: `work/scripts/kcfi_hash.py` (xxHash64 kCFI id of a mangled type) and `work/scripts/kcfi_solve_example.py` (brute force of tag names against the stock hash words).

## Lessons

- `-ftrivial-auto-var-init=zero` zero-fills at the declaration in declaration order and the stack layout follows declaration order too; an explicit `memset()` after the declaration of a struct local
  (DSE removes the auto-init) changes the zeroing order without changing the layout - needed to reproduce stock `set_config()`.
- Range checks: `x != '0' && x != '1'` and `x == a || x == b` compile to different compare shapes; the stock `sub/cmn/b.ls` shape came from the `!=`/`&&` error-condition form.
- The 6.6.57 `__access_ok()` trick (see debug_ext) is also needed here for the inlined `copy_from_user()` (three TUs).
- Several unsigned/signed details were recoverable only from compare condition codes (`b.hs/b.lo` vs `b.ge/b.lt`): `num`, `max`, `cur_pos`, `total` are `unsigned int`.


## G55a (22.10.2026) - module-finish pass: block-level verdict for EVERY previously differing function (GLOBAL OS4.0.0.7 stock unless noted; no hardware, no git, no phone)
Method: real Kleaf build of the sources below (scratch <work-dir> bazel-out removed), compare_ko_v2 + preamble_cmp + tbl_audit3 (data tables by CONTENT), then for every differing function the raw `llvm-objdump -dr` listing of stock vs ours was read block by block (compact views + alpha/hunk diffs in work\a17\review\g55a_ev\ev\<module>.<function>.{stock.c,ours.c,diff,pdiff}; symcmp5b/s5ms used only as a corroborating filter). Evidence is disassembly of stock code: local review material, never export.
Build: real Kleaf build md5 17d9c2a4de4df9de3942b8660e3a434a (d_task.c md5 3737df821a79c8d62f9b829247010a57, CRLF kept). Function set 42, kCFI 41/41, objects 19 (**no renamed statics left**), anon-data PASS, bug-table 1 (line equal), imports 70 + CRC, byte-identical 38 of 42 (unchanged).
Source change: the static proc_ops `proc_debug_into_D_ops` renamed to the stock symbol `debug_into_D_ops` (kallsyms name only; tbl_audit3 reported it as the only renamed object).

| function | verdict | argument (raw-listing blocks) |
|---|---|---|
| detect_list_write | EQUIVALENT-PROVEN | (a) `cmp x23,#0x64; b.hi` vs `cmp x23,#0x65; b.hs` (>100 == >=101); (b) the clamped remaining size: stock clamps x25 to 0 only in the newline-found path and uses the clamped x23 for strnlen, ours clamps once at the join; in the not-found && w26>0x200 case x23 = 0, strnlen(...,0) = 0 and `cmp x0,x23; b.hs` traps in BOTH, so the unclamped x25 is never used; (c) mov 0x200 / cmp / csel instruction order. symcmp5b flags 444 same-atom path regions, all on the A24/A25 clamp atom described in (b). |
| init_D_task | EQUIVALENT-PROVEN | Stock sets w19=-12 in three error blocks, ours shares one `mov w19,#-0xc` tail for two of them; the "hook register failed" block (unregister + printk 0x30d) is placed before the kfree cleanup in stock and after it in ours with a branch back; static ops/netlink cfg names ($d.88 vs .L__const.sendmsg_netlink_init.cfg) are equal in content (tbl_audit3). symcmp5b CLEAN-POL. |
| init_signal_record | EQUIVALENT-PROVEN | 5 hunk lines: adrp/ldrb order of sig_monitor_defualt vs g_sig_driver; shift mask construction (1<<byte or 1<<byte2), zeroed g_sig_driver words equal. symcmp5b CLEAN-POL. |
| signal_record_hook_handle | EQUIVALENT-PROVEN | ldp/add/stp of the two counters at +0x10/+0x14 with w9/w10 exchanged: stock ldp w10,w9 then stp(w10+1 , w9+1), ours ldp w9,w10 then stp(w9+1, w10+1): same stored values. |

Evidence files: `work\a17\review\g55a_ev\ev\process_monitor.<function>.{stock.c,ours.c,diff,pdiff}`.
