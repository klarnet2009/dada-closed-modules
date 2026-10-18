# cameramsger - PARTIAL (14 of 22 functions byte-identical vs the OS4 stock, re-checked by g4 17.10.2026)

## G1al (19.10.2026) - ioctl/read/release/snd_message re-read (no source change; first line unchanged, still PARTIAL)
- Emulation multiset: cameramsger_ioctl 1 (one `ldurb` of a local), cameramsger_read 9, cameramsger_release 16, cameramsger_write 17, snd_message 54. No constant, offset or call-set difference in ioctl/read/release (`+1 CALL snd_message` is only a relocation NAME: the stock calls the local copy through `.text+off`).
- cameramsger_read: the stock evaluates `&readers[idx].msgs[front]` four times with its own `cmp w19,#0xf` ring-index trap before each use (copy_to_user dest, strnlen, the printk `%s` and the printk strnlen argument); ours lets GVN fold the last one into the earlier check (3 instead of 4: `umaddl x2`, `mov #0x87c/#0x8838`, `cmp #0xf; b.hi` missing). Tried printing through `r->front` / `r->msgs[r->front]` (reload after the call): no change, reverted. The stock `front` is read separately in both arms after copy_to_user (ours too).
- cameramsger_release: the stock walks `readers[i]` with a pointer (`add #0x1a8`, `ldr [x,#0x190]`) and spells the `die` test as `tbz`, ours multiplies (umaddl) and uses cset; the debug lock/unlock pairs are as before. Not changed (3 iterations of G1ak + this read).
- snd_message ring-index check: stock `cmp w,#0x10; b.hs` plus `madd` with negative constants (`mov #-0x87c`, `#-0x8838`: signed index, subtraction form) and a pointer-offset check `cmp #0x87b`; ours `cmp #0xf; b.hi` and positive umaddl. The `% MSGER_MSG_NUM` results are provably in range for us, the stock trap form shows the index there is a signed int loaded from memory; the exact source (probably `int rear = r->rear` used as signed index, srem) not yet found.

## G1ak (18.10.2026) - snd_message length gate (first line unchanged, still PARTIAL: 14 of 22 byte-identical, the same 8 differ)
- Real Kleaf + compare_ko_v2 + preamble_cmp: function bytes 8 of 22 differ (as before), set 22 / binding / objects PASS, kCFI: only the known `debug` (stock u8 return). Emulation multiset total 11 functions, ~295 -> 211 differing insns.
- **Real fix: snd_message** the stock returns -1010 immediately when `len < 8` (`cmp w0,#8; b.lt`, before the scan loop and without the debug print); ours failed later and so accepted messages of 5..7 bytes. Now `if (len < 8) return -1010;`. The caller cameramsger_write passes `snd_message(count)` after its own `count < 5` gate.
- snd_message 136 -> 54: locals declared in the order that gives the stock stack layout (hdr, data, second, first), the scan loop reads `bp[i]` (the stock bound is the whole c_msger_dev object, 0x804), the two copy loops count with a separate 64-bit index (`first[j] = bp[i]`). Left: sanitizer forms (stock `cmp w8,#0x10` check on the ring index, `madd` with signed index) and two more callee-saved registers.
- cam_sched_w_pids (90): tail read against the stock (ns->offset / pids / ctl_cpus copy, flags[cpu] with 8-entry trap, rcu_assign + synchronize_rcu + kfree): same behaviour; the difference is the bubble sort (stock fully unrolls the inner loop 5x with pointer-based local-bounds checks `cmp x,#0x13`, ours uses array-bounds checks).
- cameramsger_write / release / read / select_task_rq_fair multiset: slot order of the `hit` / `val` locals and `debug()` evaluated twice in release; no constants or offsets differ.
- Hacks: none (declaration order is codegen only).

## G1ak (18.10.2026) - snd_message length gate (first line unchanged, still PARTIAL: 14 of 22 byte-identical, same 8 differ; DO NOT treat as strict)
- Real Kleaf + compare_ko_v2 + preamble_cmp: function bytes 8 of 22 differ (same as before), set 22 / binding / objects PASS, kCFI: only the known `debug` (stock u8 return). Emulation multiset total 11 functions, ~295 -> 211 differing insns.
- **Real fix: snd_message** the stock returns -1010 immediately when `len < 8` (`cmp w0,#8; b.lt`, before the scan loop and without the debug print); ours only failed later and so accepted messages of 5..7 bytes ("a|b|c" shapes). Now `if (len < 8) return -1010;`. Callers are cameramsger_write -> `snd_message(count)` after the `count < 5` gate.
- snd_message 136 -> 54: locals declared in the order that gives the stock stack layout (hdr, data, second, first), scan loop reads `bp[i]` (stock bound is the whole c_msger_dev object, 0x804), the two copy loops count with a separate 64-bit index (`first[j] = bp[i]`). Left: sanitizer forms (stock `cmp w8,#0x10` array check on the message ring index, `madd` with signed index) and two more callee-saved registers.
- cam_sched_w_pids (90): tail read against the stock (ns->offset/pids/ctl_cpus copy, flags[cpu] with 8-entry trap, rcu_assign + synchronize_rcu + kfree): same behaviour; difference = the bubble sort (stock fully unrolls the inner loop 5x with pointer-based local-bounds checks `cmp x,#0x13`, ours uses array-bounds checks).
- cameramsger_write/release/read/select_task_rq_fair multiset: slot order of the `hit`/`val` locals and `debug()` evaluated twice in release; no constants or offsets differ.
- Hacks: none (declaration order is codegen only).


## G11 (18.10.2026) - kCFI prototypes of the submodule callbacks fixed, first line unchanged (still PARTIAL, 14 of 22)
- The multiset audit flagged `cam_sched_ioctl`, `cam_sched_w_pids`, `get_tgid_by_cmdline` (address in .rodata/.text, kCFI hash different from the stock: indirect call through `submodules[]` would trap). Cause found with `g11_tools/kcq.py`-style brute force: the stock `hit` out-parameter is `unsigned char *` (hash 0x9058c5bb = `int (struct file *, char *, int, unsigned char *)`, 0x8611c10d = `void (struct file *, unsigned int, unsigned long, unsigned char *)`), ours was `bool *`. Changed to `u8 *hit` (prototypes, the `struct cam_submodule` function pointers, the two `hit` locals). Result: preamble mismatches of address-taken/global functions 4 -> 1 (emulation and Kleaf preamble_cmp); function bytes unchanged (14 of 22 identical, same 8 differ: register allocation).
- Left (harmless): `debug` (stock `unsigned char debug(int)` has a kCFI preamble 0xef5250c9, ours `int debug(int)` has none: not address-taken in ours; the stock type is u8, not changed to avoid new codegen drift).
- cameramsger_read/write/release/snd_message REAL-by-multiset (audit): unchanged, still register allocation / stack slots.

g4j (17.10.2026): real Kleaf build re-run against the OS4 stock: function set (22), relocations, objects, imports (53)+CRCs, modinfo OK; 14 of 22 byte-identical, 8 differ (read, write, release, ioctl, snd_message, xmi_rvh_select_task_rq_fair, init_module, cam_sched_w_pids). NOTE: no function is MISSING (the "8 missing functions" in the goal text are these 8 byte-differing ones); no source change, all 8 are register-allocation / block-layout differences documented below.

g4d (17.10.2026): real Kleaf build re-run: function set (22), relocations, objects, imports (53) + CRCs, modinfo OK; 14 of 22 functions identical, 8 differ (read, write, release, ioctl, snd_message, xmi_rvh_select_task_rq_fair, init_module, cam_sched_w_pids). No change made.


Re-checked against `<work-dir>` (real Kleaf ThinLTO build). The only OS4 change that mattered for the "everything else" checks: the ThinLTO-promoted object is now named
`c_msger_dev.llvm.400187243914933332` (was `...12840717646342289955` in HyperOS 3): updated in `cameramsger.c`; after that function set (22), all relocations (resolved targets), global objects, imports (53) + CRCs and modinfo are OK.
The 8 functions that still differ are the same as before (read/write/release/ioctl/snd_message/xmi_rvh_select_task_rq_fair/init_module/cam_sched_w_pids: register allocation / block layout only).

## Earlier rounds
Real Kleaf ThinLTO build (defconfig method): function set OK (22), relocations of ALL functions identical (resolved targets),
global objects (name,size) OK, imports + CRCs OK (53 symbols), modinfo OK.

Byte-identical: debug, cameramsger_open, cameramsger_poll, reg_reader, find_reader_by_name_or_tgid, cam_sched_ioctl,
clear_cam_sched_data_entity, cam_sched_init, xmi_rvh_is_cpu_allowed, xmi_can_migrate_task, xmi_sched_newidle_balance,
effect_cpu_action, get_tgid_by_cmdline, find_pid_cmdline.

Still different (word diff vs stock in the ThinLTO-emulated loop; logic is the same, register allocation / block layout / expression shape):
 cameramsger_read 62, cameramsger_write 187, cameramsger_release 188, cameramsger_ioctl 283, snd_message ~300,
 xmi_rvh_select_task_rq_fair 104, init_module 95, cam_sched_w_pids 194 (was 393; second iteration round).
 * read: the failure path of copy_to_user is selected in 64-bit (csel x) in ours, 32-bit (csel w) in stock; stock reloads `front` in both branches (using r->front directly
   everywhere makes it worse, 96 words, and changes the register assignment of `front`).
 * write: same code but different stack slot assignment of locals (hit/val/p) and register use of `file`.
 * release: the "task is dead" computation is sunk below put_task_struct() in ours; stock computes it before.
 * ioctl: stock keeps mutex_lock inside every debug case (cases 0x40044dcb..dce) instead of one shared lock; our sources get the lock sunk.
 * select_task_rq_fair: stock keeps hit/flag/offset in callee-saved registers and tests (flag==4?hit:0)/!offset/!(flag==2?hit:1)
   as separate csel results; every equivalent boolean spelling we tried is folded to the same simpler form.
 * init_module: only register allocation (an extra register holds &readers[i].wait_queue across the memset call).
 * snd_message: stock has first[60]@sp+8, second[60]@sp+0x48, data[2048]@sp+0x88, a 124-byte zero-initialised header (int, from[60], to[60]) and pos[3]; the
   header is copied into msgs[rear] and data is memcpy'd separately (struct msger_message = {int; char from[60]; char to[60]; char data[2048]} with a header sub-struct
   reproduces this). Reading the source buffer through a `char *bp = c_msger_dev.buf` pointer reproduces stock's whole-object (0x3c320) llvm.objectsize bounds trap
   instead of a 2048 clamp. Not matched: stack-slot order vs. initialisation order (all 120 declaration permutations tried, none reproduces stock's
   'pos; first; second; hdr; data' init order together with its frame layout) and the 64-bit induction variable of the copy loops.
 * cam_sched_w_pids (rewritten from the asm this round): stock order of locals {cpu_val=0; p=buf; pids[5]={0}; mask zeroed after the NULL check}, `unsigned npids` (npids > 4 is
   an unsigned compare, array store needs no check), debug print `[%s] cpumask:%d` after kstrtoint, `(int)strnlen(tok,60)-1 >= 1`, `for_each_possible_cpu(i) if ((cpu_val >> i) & 1)`,
   sort guarded by `if (npids)`. Remaining difference is only the bubble-sort: stock keeps the i/j loops with array-bounds traps (compiler does not use the npids<=5 range),
   ours gets them unrolled and range-folded. All int/long/unsigned combinations of i/j tried (194-201 words).

Recovered facts (all verified against the asm):
 * struct msger_dev { char name[25]; char version[31]; class*@0x38; int major@0x40; dev_t devt@0x44; device*@0x48; struct file_operations fops@0x50;
   struct mutex g_lock@0x158; int reader_count@0x188; struct msger_reader readers[7]@0x190; int debug@0x3bb18 (0x4cc msger logs, 0x4cd sched logs);
   char buf[2048]@0x3bb1c }  size 0x3c320, ThinLTO-promoted symbol `c_msger_dev.llvm.12840717646342289955` (GLOBAL HIDDEN via asm label).
 * struct msger_reader (0x8838) { tid,pid,used,rear,front,pad, file *fp@0x18, char name[60]@0x20, struct msger_message msgs[16]@0x5c (0x87c each: int@0, from[60]@4, to[60]@0x40, data[2048]@0x7c), wait_queue_head_t@0x8820 }.
 * struct cam_sched_data (56 bytes) { u8 flags[8]; u8 offset@8; int pids[5]@0xc; cpumask@0x20; ... }, RCU pointer `csdata`.
 * ioctl 0x40044dcb..dd1 (debug levels, dump, switches) and 0x40044dd0 (sched dump); write commands "swh|n", "reg|name", "snd|to|from|msg",
   "ctl|cpumask|allow/neverallow|cmdline...", "ctp|cmdline".
 * stock task_struct offsets match the GKI headers: pid 0x618, tgid 0x61c, comm 0x830, thread_pid 0x680, files 0x860, nr_cpus_allowed 0x48c, mm 0x5a0.
Techniques that mattered:
 * csdata + the zero-size lock keys share one .bss address in the stock link; the first symbol there is `cameramsger_init.__key`, so csdata is named that
   (asm label) and the lock key is passed as that address through an `asm("adrp/add")` (no CSE) -> all relocation targets identical.
 * `current` is re-evaluated in each loop test but the value is reused later: `(cur = current)->tgid == r->pid` reproduces the register reuse.
 * old (smp_mb-less) poll_wait: helper without barrier; `strlen(ptr_var)` vs `strlen(field)` gives different FORTIFY bounds (2048 vs 2044).
 * memset on struct fields beyond the field size triggers __write_overflow_field (extra import!): use `(char *)obj + offsetof(...)`.
