# mi_damon - EQUIVALENT vs the GLOBAL OS4.0.0.7 stock (G55a 22.10.2026: compare_ko_v2 all 190 functions byte-identical, tables/imports/CRC/kCFI PASS; only the build-system buildmeta tags differ); NOT hardware-tested, DO NOT LOAD until a hardware test

## G27b (20.10.2026) - kdamond_fn read by hand (first line unchanged; no source change)
- symcmp5 stays INCONCLUSIVE (BDD cap, also with S5TMO=900). By hand against the global stock (md5 ba1e74b31ab8403626cf51ca41f679c7): 1596 vs 1594 instructions, the call multiset (relocation targets) is identical, and the register-name-agnostic instruction multiset differs only in: spill/reload forms (`ldr/str/ldp [sp]`), one `eor/csinc/tbnz` vs `csel/cmp/b.ne` form of the same boolean `(i < nr_subs - 1) && sz_region > 2*(unsigned)damon_min_vma` (inlined damon_split_regions_of: checked, equal), and 2 extra `adrp+ldr damon_min_vma`. Verdict CODEGEN.
- Only observable nuance: stock reads `damon_min_vma` once per region (LTO makes the global internal, so the load is hoisted over the loop calls), ours reloads it per iteration (non-LTO build, external global). Only a concurrent sysfs write of min_vma could see the difference; not changed.

## G17a (19.10.2026) - global OS4.0.0.7 re-measurement and function classification (first line unchanged unless noted: still PARTIAL, no hardware test)
Global OS4.0.0.7 stock (<work-dir>) vs CN OS4.0.0.6 stock: code-identical (cn_vs_gl.csv, compare_ko_v2 stock-vs-stock: only buildmeta/scmversion differs). Real Kleaf build (scratch <work-dir> tag g17a, bazel-out removed) vs the GLOBAL stock: function set 190 OK, kCFI 178 OK, imports OK; 189 of 190 byte-identical; kdamond_fn differs and is symbolically identical (symcmp4 CLASS-A-auto: 315/315 facts, 0 guard differences) = pure CODEGEN (stack-slot numbering). The jump-table/altinstructions failures follow from that one function. 0 BEHAVIOUR differences, no source change.
Tools: <work-dir> (kb17.sh = Kleaf build + compare_ko_v2 + preamble_cmp, sym17.sh = symcmp4 on every differing function, str17.py = printk format scan, fd17.sh = disassembly diff), results in res/<module>.cmp / .sym. Hacks: none added; earlier hacks (forged CRC / stubs / codegen hints) unchanged and still listed above.


g1aj (18.10.2026): REAL difference found by the multiset audit in `kdamond_fn` (inlined damon_split_regions_of): the stock tests `sz_region > 2 * damon_min_vma` with the GLOBAL variable (`ldr w8,[damon_min_vma]; lsl w28,w8,#1`, 32-bit, default 0x8000 = split only regions larger than 64 KiB); ours compared with the compile-time `2 * DAMON_MIN_REGION` (= 2 * PAGE_SIZE, 8 KiB). Now `2 * (unsigned int)damon_min_vma` (core.c). The ALIGN_DOWN of the split size stays 0x1000 (stock `and #0x1fff...f000`). Real Kleaf + compare_ko_v2 after the change: still 189 of 190 functions byte-identical, only kdamond_fn differs (jump-table/altinstructions offsets +32 follow from it), function set, binding, kcfi 181, objects, anon-data, bug-table, exports, imports, modinfo PASS. The residual of kdamond_fn is stack-slot layout (emulation multiset 89-99, no constant or call left).

g11c (18.10.2026): kCFI prototype fixed: stock `damon_vma_need_sample` has kCFI hash 0xa9d20869 = `bool (struct vm_area_struct *)` (found with g11c_tools/kcs2: _ZTSFbP14vm_area_structE; we had `int`, 0xfddb2193). Changed int -> bool in ops-common.c/.h. Real Kleaf + compare_ko_v2 + preamble_cmp: kcfi-preamble PASS (181 words identical, was 1 mismatch), function bytes unchanged (only kdamond_fn differs, as before).

g4j (17.10.2026): real Kleaf build re-run: function set (188), relocations, objects, imports (97)+CRCs, modinfo OK; 187 of 188 byte-identical, only kdamond_fn differs. Not touched in g4j.

g4d (17.10.2026): real Kleaf build re-run: function set (188), relocations, objects, imports (97) + CRCs, modinfo OK; 187 of 188 identical (`kdamond_fn` differs). No change made.


Re-checked against `<work-dir>` (real Kleaf ThinLTO build). OS4 change: the ThinLTO hashes of the promoted statics (`damon_sysfs_ul_range_groups.llvm.10995319723490364896`,
`damon_sysfs_schemes_{release,groups,for_damos_callback,region_idx}.llvm.14731241909622278991`): updated in `sysfs-common.c` / `sysfs-schemes.c`; afterwards function set (188), relocations, objects, imports (97) + CRCs, modinfo OK
and only `kdamond_fn` differs (as in HyperOS 3).

## Earlier rounds
Real Kleaf build (`re_push.sh mi_damon`, ThinLTO via `defconfig`, last run after all edits): function set (188) OK, function relocations OK, global objects OK,
exported symbols (none) OK, imported symbols + CRCs (97) OK, modinfo OK.  Function bytes: all identical except `kdamond_fn`.

## What remains: `kdamond_fn` (same code, different register/stack-slot allocation)
Same 1594 instructions in the same block order; same instruction mix; the difference is register allocation / spill-slot numbering only:
the stock keeps `max_nr_accesses` completely in a stack slot (`str w0,[sp,#0x64]`, one `ldr`) and rematerialises the `0x1000` of `damon_region_sz_limit()`,
ours keeps both in callee-saved registers (w27/w24) and reloads them (`ldr w27,[sp,#0x34]; mov w24,#0x1000`) at six block starts.
Root cause found, exact trigger not: the stock's 3rd scheme loop of `kdamond_apply_schemes()` (the `next_apply_sis` update) is NOT loop-rotated
(preheader `ldr x8,[x25]; b TEST`), ours is rotated (guard in the preheader).  Whenever that loop is kept unrotated the allocation becomes the stock's:
`max_nr_accesses` / `0x1000` spilled identically, block order identical, and only the numbering of 3-5 spill slots differs
(stock: next_ops_update_sis [sp,#0x48], merge invariants [sp,#0x40]/[sp,#0x50]/[sp,#0x68]; ours has two of them swapped so `stp`/`ldp` pairs form: 1577 instead of 1594 insns).
Ways to keep that loop unrotated that were found (none is bit-exact, all leave the slot-numbering diff, ~150-190 diff lines instead of 410):
- `asm("" : "+r"(lp))` (in-out) in the loop-header test of the loop written over a `struct list_head *lp` (`core.v7` in `work/re/poc_perfA/`; +2 branch hints 151 lines),
- `static __always_inline` on `damon_split_region_at()` (changes the pre-link inlining order; 187 lines, no asm hack: `core.ai`),
- `__builtin_assume`/`__builtin_unreachable` in the header (RA differs more).
LoopRotate details: a header with an inline-candidate call is not rotated in the ThinLTO pre-link pass and is rotated at post-link unless something persistent
(llvm.assume, in-out asm, a real call) stays in the header; the stock's loop 3 has nothing visible in its header, so the real trigger is a tiny pre-link IR difference
(phi operand order, a few dead stores) that I could not recover from the disassembly.
Searches done with the 0.7 s/variant server oracle (`work/scripts/perfA_batch.py`, `perfA_bt1.sh`): likely/unlikely and `__builtin_expect_with_probability` on all branches (singles, pairs, ~3000 random combos),
inline/always_inline/noinline on all 26 helpers (singles, pairs, hill-climb), function-definition order (200 permutations), declaration order, adjacent-statement swaps,
equivalent spellings of the `(last*60 + cur*40 + 50) / 100` smoothing, loop-3 formulations (safe/while/do-while/helper), M (max_nr_accesses) hint tricks.  Best result 143-151 diff lines (never 0).
The shipped `core.c` is the natural source (410 diff lines, no hacks).

## What it is
Xiaomi's out-of-tree copy of DAMON (core.c, vaddr.c, ops-common.c, sysfs*.c; base = Linux 6.7-ish DAMON as in the MiCode bixi/onyx kernels, `work/re/poc_perfA/damon_ref/bixi`,
adapted by `work/re/poc_perfA/adapt_damon.py`) built as one module `mi_damon` with Xiaomi changes recovered from the disassembly:
- sysfs root is `/sys/kernel/mi_damon` (kobject under `kernel_kobj`), extra files `admin/debug_log` and `monitoring_attrs/min_vma`; globals `debug_log`, `damon_min_vma` (=0x8000) + get/set helpers.
- trace system `mi_damon`; `damon_aggregated` tracepoint has the 3-argument form (target_id, region, nr_regions).
- region construction from VMAs: `damon_get_vmas()` (maple tree + per-VMA lock walk, skips special / tiny (< damon_min_vma) / `dalvik-` named / stack VMAs),
  `damon_set_regions_with_vmas()`, `damon_vma_need_sample()`; `damon_va_init()` tries VMAs first and falls back to the three-region method, `damon_va_update()` uses VMAs.
- `damon_va_apply_scheme()` is a stub (returns 0): the module only monitors; the sysfs scheme filters are not applied (`damon_sysfs_set_scheme_filters` and the memcg path helpers are gone).
- `kdamond_reset_aggregated()` smooths `nr_accesses = (last*60 + cur*40 + 50) / 100` before it is traced/reset; the merge threshold is `max_nr_accesses / 4` (not /10).
- no MMU-notifier calls in the young/mkold paths, `ktime_get_coarse_real_ts64`, `READ_ONCE(*pte)` instead of `ptep_get()` (the stock headers are 6.6.57; the build server tree is 6.6.144 and its
  `ptep_get`/`mas_pause`/`poll_wait` differ).
- module init creates the `damon_region` cache then calls `damon_sysfs_init()` and `damon_va_initcall()`.

## What mattered for byte identity
- `defconfig` with `CONFIG_LTO_CLANG_THIN=y` (cross-file inlining, `import-instr-limit=5`).
- ThinLTO-promoted statics are GLOBAL HIDDEN `name.llvm.<hash>` in the stock: reproduced with `__asm__("name.llvm.<hash>")` + hidden visibility.
- the stock .bss is merged: `damon_new_ctx.__key` lies at the address of `nr_running_ctxs`; reproduced with an explicit shared section.
- `damon_set_regions_with_vmas` (the Xiaomi two-pointer merge): the loop skeleton decides everything.  Working shape:
  `while (&r->list != head_t) { while (&vr->list != head_vma) { if (destroy case) {...; goto next_r;} if (insert case) {...; vr = next; continue;} intersect...; goto next_r; } break; next_r:; }`,
  the intersect scan starts at `next = r` (tests `next->start < vr->end && next->end > vr->start`, in that order) and declares `next` before `last`.
  (the r-test as rotated outer-loop header makes `&r->list` a phi, the vr test as inner-loop header makes `&vr->list` a phi; this also gives the stock's spill slot for LIST_POISON2).
- codegen workarounds (noted in the source): `asm("" : "+r"(size))` keeps the 64-bit `end - start` before the `(int)` compare in `damon_vma_need_sample` / `damon_get_vmas`.
- fast loops without bazel: `bash work/scripts/perfA_qd.sh mi_damon --lto [-d -f <func>] [--regnorm]` (ThinLTO emulation, ~7 s); `perfA_batch.py` (many variants in parallel),
  `perfA_ir.sh` (per-pass IR/MIR dump), `perfA_dis.sh` (disassembly of ours/stock).


## G55a (22.10.2026) - module-finish pass: block-level verdict for EVERY previously differing function (GLOBAL OS4.0.0.7 stock unless noted; no hardware, no git, no phone)
Method: real Kleaf build of the sources below (scratch <work-dir> bazel-out removed), compare_ko_v2 + preamble_cmp + tbl_audit3 (data tables by CONTENT), then for every differing function the raw `llvm-objdump -dr` listing of stock vs ours was read block by block (compact views + alpha/hunk diffs in work\a17\review\g55a_ev\ev\<module>.<function>.{stock.c,ours.c,diff,pdiff}; symcmp5b/s5ms used only as a corroborating filter). Evidence is disassembly of stock code: local review material, never export.
Build: real Kleaf build md5 4087beb119d5dbc106a579be951286cb (core.c md5 337ada48d05c0e15d314756a5b5d4454). compare_ko_v2 (full): function set 190, **function bytes all 190 identical**, kCFI 181/181, objects 175, anon-data 2, jump-table 18, other-tables 62, bug-table 0, imports 97 + CRC, modinfo PASS; the only FAIL is buildmeta (intree/scmversion/built_with/srcversion, build-system tags).
FIX (real codegen difference found by reading kdamond_fn): damon_split_regions_of() read `damon_min_vma` in the loop condition (3 loads per region, 64-bit `ldr x8` + `lsl w`), the stock reads it ONCE before the region loop as a 32-bit value (`ldr w8,[x8]; lsl w28,w8,#1`). Source: `unsigned int min_sz = damon_min_vma;` before the loop, condition `sz_region > 2 * min_sz`. Semantics identical (u32 truncation as before); with it kdamond_fn is byte-identical (1513 insns).

| function | verdict | argument (raw-listing blocks) |
|---|---|---|
| kdamond_fn | FIXED -> IDENTICAL | Hunks before the fix (63): stp/ldp vs str/ldr pairs of spill slots, `damon_min_vma` loaded 3x as 64-bit vs once as 32-bit, region_cache reload, `eor/csinc/tbnz` vs `csel/cmp/b.ne` form of (i < nr_subs-1 && sz > 2*min); after the source change the listing is byte-equal. |

Evidence files: `work\a17\review\g55a_ev\ev\mi_damon.<function>.{stock.c,ours.c,diff,pdiff}`.
