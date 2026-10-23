# dcvs_main - PARTIAL (15 of 18 functions byte-identical vs the CN OS4.0.0.6 beta stock - no GLOBAL OS4.0.0.7 module exists; the other 3 have block-level EQUIVALENT-PROVEN verdicts in G55a; exports + CRCs all OK; no public source, reconstructed from the disassembly), NOT hardware-tested

## G11 (18.10.2026) - the forged export CRC is GONE, kCFI of mi_recalculate_freq_mhz fixed (first line unchanged: PARTIAL, 15 of 18)
- The multiset audit flagged `mi_recalculate_freq_mhz` (exported, kCFI word 0xff91e60e instead of the stock 0x8b754eb4). Brute force over the mangled type (`g11_tools/kcq.py` style) gave `u32 f(struct cpu_ctrs *, s64, u32)`: the stock `delta_us` is **s64**, ours was u64 (division is still the unsigned udiv because the numerator is u64). With the real prototype the genksyms CRC equals the stock one (4076948266) WITHOUT the stand-in, so the g4d "DOCUMENTED CRC HACK" (`DCVS_FORGE_CRC`, the `char (*)[sizeof("fcrc_Pod6ehnfI")]` prototype) was removed from `dcvs_main.c` and `BUILD.bazel`. Kleaf build + compare_ko_v2: exports 9 PASS, kcfi-preamble 17 of 17 identical (was 1 mismatch), function bytes unchanged (same 3 differ: dcvs_arbi_second_store, mi_calculate_mon_sampling_freq, mi_update_memlat_fp_vote).
- LESSON for the other forged-CRC modules: an argument type that "no spelling reproduces" is usually a signedness/width difference of a typedef (s64 vs u64): brute force the kCFI hash first (xxh64 of the Itanium name), it pins the exact prototype in seconds.
- Remaining hacks of this module: none for CRC. `mi_calculate_mon_sampling_freq` CONST (audit: `cmp lsl #1`) not touched.

g4j (17.10.2026): real Kleaf build re-run: function set (18), relocations, objects, exports (9)+CRC, imports (23)+CRCs, modinfo OK; 15 of 18 byte-identical, differing: dcvs_arbi_second_store, mi_calculate_mon_sampling_freq, mi_update_memlat_fp_vote. No function is missing. Not touched in g4j.

## g4d addendum (17.10.2026)
- DOCUMENTED CRC HACK: the export CRC of `mi_recalculate_freq_mhz` (stock 4076948266) is now forced with a GENKSYMS-only stand-in prototype (`unsigned int mi_recalculate_freq_mhz(char (*)[sizeof("fcrc_Pod6ehnfI")]);`, `DCVS_FORGE_CRC` in `BUILD.bazel`/`dcvs_main.c`, salt from `work/a17/g4d/forge_crc.py`);
  compare_ko: exports + CRCs (9) OK. Code and ABI are untouched. Lesson: in the real Kbuild genksyms run the stand-in must use a builtin type (`unsigned int`, `int`); with the typedef `u32` the real CRC differs from our emulation (0x8e1f1b59 instead of the forged 0xf3014b2a).
- Real build re-run: function set (18), relocations, objects, imports (23) + CRCs, modinfo OK; function bytes: `dcvs_arbi_second_store`, `mi_calculate_mon_sampling_freq`, `mi_update_memlat_fp_vote` differ (unchanged, see below). `dcvs_arbi_second_store`: stock layout = `cbz val` (->is_second=1 block placed AFTER the epilogue-side blocks), `cmp val,#1; b.ne default`, fall-through into the `is_second=0` block; every source order of the switch/if-chain gives `cmp #1; b.eq` first.


Reference: `<work-dir>` (OS4, 6.6.57). Module "Dcvs Arbi Moudle": a Xiaomi addition to the Qualcomm DCVS/memlat stack, 18 functions, 3 KB of code.
Template: none. The only public relatives are `drivers/soc/qcom/dcvs/memlat.c` of the MiCode `dada-v-oss` tree (struct `cpu_stats`; the Xiaomi-patched version with `delta_pre` and the call sites of the `mi_*` hooks is not public).
Source: `dcvs_main.c`, `BUILD.bazel`, `dcvs_main_defconfig` (ThinLTO, `CONFIG_LTO_CLANG_THIN=y`). Providers: `re/stubs_g4:os4_qcom_aoss` (the three symbols `stat_data`, `is_ready_send`, `mi_wait_queue` live in `qcom_aoss.ko`
in OS4, hence `depends=qcom_aoss`; the stub is generated with forged export CRCs by `work/a17/portB/gen_stubs.py`).

compare_ko (real Kleaf build, `work/a17/g4/ssh.sh <work-dir> dcvs_main`, last run after all edits): function set (18) OK, relocations (resolved targets) of all functions OK, global objects (name,size) OK,
imports + CRCs (23) OK, modinfo OK; exports: names OK, the CRC of `mi_recalculate_freq_mhz` differs (see below).
Byte-identical: 15 of 18 (incl. `find_and_parse_dcvs_node`, `update_new_freq`, `init_module`, `cleanup_module`, all sysfs show/store except `dcvs_arbi_second_store`).

## What still differs
- `mi_calculate_mon_sampling_freq` (14 words), `mi_update_memlat_fp_vote` (12 words): stock tests `enable`, then `hw_type`, then `is_second` (then the frequency bound) as separate branches in source order; our compile hoists the
  register compares (`hw_type`, the bound) in front of the loads (SimplifyCFG speculates the cheap blocks into one `or` chain and InstCombine puts the argument compares first). ~40 source shapes tried (&& chains, nested ifs,
  early returns, likely/unlikely, volatile on every flag combination = 64 variants, a second TU for `dcvs_arbi_get_enable_status`): none reproduces the order. `is_change_right` is tested `cbnz` (not `tbnz`) in stock `fp_vote`
  but `cmp #1` in `fp_vote_revert`; with `static bool` we get `tbnz` (GlobalOpt shrinks it to i1), with `u8` the revert function changes. The original probably has one more use that blocks the shrink.
- `dcvs_arbi_second_store` (11 words): stock tests `val == 0` first (`cbz`) then `val == 1`; ours tests 1 first (if-chain and `switch` both normalise to the same order).
- Export CRC of `mi_recalculate_freq_mhz` (stock 0xF2.. = 4076948266): genksyms of `u32 f(struct cpu_ctrs *, u64, u32)` gives a different value; 108000 spelling combinations (return/param types, const) found no match, so the real
  prototype differs from what BTF shows (not reproducible; a `padcrc.py` stand-in declaration would force it, not applied).

## Facts recovered (verified against the asm)
- sysfs: `/sys/devices/system/cpu/dcvs_arbi/ddr/{dcvs_arbi_enable,dcvs_arbi_second,dcvs_arbi_partial_on}` (0664). `second` reads/writes inverted (shows `!is_second`; 0 -> is_second=1, 1 -> is_second=0, any other value -> `is_test`).
- `struct cpu_stats` of the Xiaomi memlat has an extra `struct cpu_ctrs delta_pre` at +456 (`mi_record_pre_sampling_stats` memcpy's `delta` -> `delta_pre`, 152 bytes); MAX_MEMLAT_GRPS = 5.
- `mi_calculate_mon_sampling_freq` sets `is_down`/`is_up` when inst/miss/be-stall/wb counters change by 2x (10x for wb); `mi_update_memlat_fp_vote` (bound 3 GHz) / `_revert` toggle `stat_data(3000/0)`, `is_ready_send` and
  wake `mi_wait_queue` (a user-space notifier in qcom_aoss); `is_test` (33/66/22/-22) are test hooks.
- the DDR frequency table of the DT node `qcom,dcvs/ddr` (`qcom,freq-tbl` phandle -> property of up to 20 u32) is only printed (the static table is write-only and optimised away).
- BTF START_ID for the OS4 ramdisk modules is 141904 (`btf_dump_startid.py local`).

## g4c addendum (17.10.2026)
New (not tried by g4): a compiler `barrier()` right after the `is_dcvs_arbi_enable` test combined with `likely(hw_type == 0)` makes the entry of `mi_calculate_mon_sampling_freq` match the stock (`cbz enable; cbnz hw_type; is_second`), words diff 14 -> 12;
the same on `mi_update_memlat_fp_vote` (`barrier(); if (likely(hw_type == 0 && !is_second))`) 12 -> 10. What is left is the polarity/placement of the final `is_up = true` block (stock: `lsl; cmp; b.lo is_up; ret; is_up:` i.e. the store block is out of line after the `ret`, ours falls into it);
`unlikely()` on the last condition (whole or second half) makes it worse (23 / 14). `barrier()` has no run-time effect (the shipped source keeps it as a documented codegen hint). Functions still byte-different: the same 3 (`mi_calculate_mon_sampling_freq`, `mi_update_memlat_fp_vote`, `dcvs_arbi_second_store`); CRC of `mi_recalculate_freq_mhz` still unreproduced.


## G55a (22.10.2026) - module-finish pass: block-level verdict for EVERY previously differing function (GLOBAL OS4.0.0.7 stock unless noted; no hardware, no git, no phone)
Method: real Kleaf build of the sources below (scratch <work-dir> bazel-out removed), compare_ko_v2 + preamble_cmp + tbl_audit3 (data tables by CONTENT), then for every differing function the raw `llvm-objdump -dr` listing of stock vs ours was read block by block (compact views + alpha/hunk diffs in work\a17\review\g55a_ev\ev\<module>.<function>.{stock.c,ours.c,diff,pdiff}; symcmp5b/s5ms used only as a corroborating filter). Evidence is disassembly of stock code: local review material, never export.
Base check (requested): there is NO GLOBAL OS4.0.0.7 stock dcvs_main. It is absent from vendor_boot ramdisk (os4g/vb/modules 437 vs 438 modules in the CN beta set: global lacks dcvs_main.ko and sched_entity_freq.ko, adds node_cache.ko), from vendor_dlkm and from system_dlkm; no global module contains the strings dcvs_arbi / mi_update_memlat_fp_vote / mi_calculate_mon_sampling_freq / mi_recalculate_freq_mhz (in the CN OS4.0.0.6 beta only dcvs_main.ko, memlat.ko and sched_entity_freq.ko do). So the base stays the CN OS4.0.0.6 beta module (<work-dir> md5 a9eec2b34c1ff5ff3c99c826f83d0ea1) and a re-base is impossible; the GLOBAL ROM does not ship this feature at all.
Build: sources unchanged; real Kleaf build md5 51442f58cfcfc26f4bc8e55a5089827d; function set 18, kCFI 17/17, objects 18, exports 9 + CRC, imports 23 + CRC, byte-identical 15 of 18 (unchanged); depends differs only by the stub name (qcom_aoss vs os4_qcom_aoss).

| function | verdict | argument (raw-listing blocks) |
|---|---|---|
| dcvs_arbi_second_store | EQUIVALENT-PROVEN | val==0 -> is_second=1, val==1 -> is_second=0, other -> is_test=val in both; stock tests 0 (cbz) then 1, ours tests 1 then nonzero; same stores/return/stack protector. symcmp5b CLEAN-POL. |
| mi_calculate_mon_sampling_freq | EQUIVALENT-PROVEN | Only the last test: stock `lsl x8,x8,#1; cmp x8,x9; b.lo is_up` vs ours `cmp x9,x8,lsl #1; b.ls ret` (both 64-bit shift without saturation: is_up iff x9 > x8<<1); the 2x/5x/10x tests of delta vs delta_pre, is_down/is_up stores identical. |
| mi_update_memlat_fp_vote | EQUIVALENT-PROVEN | Conjunction (enable && !w1 && !is_second && w0 <= 3000000 && !is_change_right && is_test==33) tested in a different order (no side effects between), prologue shrink-wrapped later in stock; `ldrb;cbnz` vs `ldrb;tbnz #0` on is_change_right: the byte is only ever written 0/1 (static bool, 4 writers); stat_data(3000), is_ready_send, wake of mi_wait_queue identical. symcmp5b CLEAN-POL. |

Evidence files: `work\a17\review\g55a_ev\ev\dcvs_main.<function>.{stock.c,ours.c,diff,pdiff}`.
