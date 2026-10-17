# mist - PARTIAL (67 of 70 functions byte-identical vs the OS4 stock, re-checked by g4 17.10.2026)

Re-checked against `<work-dir>` (real Kleaf ThinLTO build). OS4 change: the promoted statics of the GPU TU are `gflt_enable|gpubw_gflt_load|adreno_gflt_load.llvm.5988974010709438231`
(was `...11378079736542486605`): updated in `mist_gflt.c`; after that function set (70), relocations (all), objects, exports + CRC (44, one forged as before), imports (51) + CRCs and modinfo are OK.
Same 3 functions differ as in HyperOS 3 (`parse_cluster_node_map`, `mist_update_rel_freq`, `mist_dflt_init`).

## Earlier rounds
Module: "mist" (SoC frequency/latency tuner: DDR/LLCC/L3 memlat vote tuner, GPU frame latency tuner, load calibration glue).

Build: Kleaf ddk_module `//re/mist`, `mist_defconfig` = ThinLTO (`# CONFIG_LTO_NONE is not set` + `CONFIG_LTO_CLANG_THIN=y`),
`-ffunction-sections -fdata-sections`, `local_defines CONFIG_QCOM_DCVS/CONFIG_QCOM_PMU_LIB/CONFIG_QTI_SCMI_VENDOR_PROTOCOL/PERFC_FORGE_CRC`
(the GKI tree already ships `soc/qcom/dcvs.h` and `pmu_lib.h`; without these defines the headers give `static inline` -ENODEV stubs and the
compiler silently deletes the vote calls). 3 TUs as in stock: `mist_dflt.c` (DDR/LLCC/L3 latency tuner + proc/param glue + init/exit),
`mist_gflt.c` (GPU frame latency tuner), `mist_lc.c` (load-calibration glue). Imports are provided by `re/stubs_perfC`
(`qcom-pmu-lib`, `qcom-dcvs`, `clk-qcom` stubs, headers `soc/qcom/mist_ext.h`).

compare_ko (real Bazel build, `work/scripts/perfC/pb.sh mist`):
- function set: OK (70)
- function relocations (resolved targets): OK, all identical (incl. the `.N` uniqued names, see "hacks")
- global objects (name,size): OK
- exports + CRC: OK (44), but `mi_soc_register_freq_adjhook` (0x1bd27011) is FORGED (see below)
- imports + CRC (__versions): OK (51, genuine)
- modinfo: OK
- function bytes: 67 of 70 identical

## The 3 remaining functions
| function | ck diff | what differs |
|---|---|---|
| mist_update_rel_freq | 10 | order of the two UBSAN checks: stock tests `cpu <= 7` first then the `&frame_stats_record[idx]` bound; ours the other way round (the cpu load is sunk below the idx check). Every spelling tried (cpu local/direct, cid first, pointer-taken, `int cid = cluster[cpu]` before the rec pointer) changes the check FORMS (`cmp #8; b.hs` / `cmp #0x65; b.hi`) instead. |
| mist_dflt_init | 4 | first `for_each_possible_cpu` loop: stock `cmp x25,#0x1f; b.hi`, ours `cmp x25,#0x20; b.hs`. Polarity is decided by IR block order (see lesson 1); in ours the exit block of the loop that contains the kzalloc call sits between header and body; stock has the other order. |
| parse_cluster_node_map | 58 | register allocation / scheduling only (203 vs 203 insns): stock builds hw*3 as `add x8,x28,w19,uxtw #1`, spills cid to [sp,#0x28], keeps the "qcom,cpufreq-memfreq-tbl" pointer in x21; ours x23 without spill. |

## Hacks that are not natural source (documented)
- Forged export CRC `mi_soc_register_freq_adjhook`: `#if defined(PERFC_FORGE_CRC) && defined(__GENKSYMS__)` stand-in declaration with a salt string literal
  (`work/scripts/perfC/crcforge.py`); the genuine prototype (char *, enum SOC_FREQ_MOD, hook) could not be made to give 0x1bd27011 with ~1000 header/spelling variants.
- Dead `pr_err` string literals (five under `if (ret > 0)` at the end of mist_dflt_init, one `if (mod > SOC_DEV_NUM-1) pr_err(..)` in mi_soc_register_freq_adjhook):
  clang's module-wide `.N` uniquing counter is bumped by every distinct literal even if the code is optimised away; they make `commit_fast_vote.trace_printk_fmt.{69,75,76}`
  and `set_*_gflt_load...fmt.{7,9}` match the stock names. I did not find where the real literals are.
- `u64 e` in `struct dflt_ev_data` (stock stores a 64-bit 0xff, gives the `stp` merges; alignment becomes 8, .bss objects (name,size) still match).
- `__uninitialized` on `cpus` in parse_cluster_node_map, non-short-circuit `|` in mist_set_next_freq_hook, `__maybe_unused` slot + second `map = *slot` in cpufreq_to_memfreq.

## Lessons (reusable)
1. **Compare polarity (`cmp #N; b.hs` vs `cmp #N-1; b.hi`) comes from IR block order.** SelectionDAG inverts a conditional branch when the true successor is the next
   block in the IR function; an inverted `uge N` is then canonicalised to `ugt N-1`. clang emits the shared UBSAN array-bounds trap block right after the FIRST check in
   source order, so the position of the first `arr[i]` with a variable index (even one that is later optimised away) decides the polarity of every loop-exit / bound compare
   that ends up next to it (verified: adding `x = 1 + skip_walt_result[x & 1] * 0` in front of a loop flips `hs 0x20` to `hi 0x1f` and makes flw_irq_work SAME). Callee trap blocks
   keep their position after inlining. Check with `work/scripts/perfC/ckll.sh <dir> <file.c> <func>` (optimised IR) and `-mllvm -print-after=finalize-isel`.
2. UBSAN array-bounds checks are emitted by the frontend at source position; local-bounds (`brk #1`) are inserted late after InstCombine sank the loads: keep loads in the entry
   block (use `|`, hoisted declarations) if stock shows the check above a branch.
3. Declaration order of locals changes register/stack-slot numbering (cpus_id before i fixed cpufreq_to_memfreq). Store merging (`stp`/64-bit `str`) depends on source order of constant
   stores and field widths. `-ftrivial-auto-var-init=zero` zero-stores at the declaration; `__uninitialized` removes it.
4. 64-bit return type of an inlined helper vs `unsigned int` local decides whether clamps are 32- or 64-bit (map_coeff_freq).
5. `tbz w8,#0` after `ldrb` means `u8 flag` tested with `& 1` in an early-return form (`if (!(flag & 1)) { ..; return false; }`), not `bool`/volatile (mi_gpu_flt_working).
6. `likely()` on pointer `==` matters (LLVM predicts pointer equality unlikely): mist_gflt_stop.
7. `if (!a) return; if (!b) return;` vs `if (!a || !b) return;` changes bool tests (`cbz` vs `cmp #1`); `&` forms of `(a > b) & (c > d)` are all canonicalised the same, `&&` with the value computed up front matched.
8. genksyms only expands struct bodies that are DEFINED at the EXPORT_SYMBOL point: the include set of the stock TU decides export CRCs (gflt TU = module/kernel/devfreq/workqueue only; dflt TU needed
   `linux/interrupt.h`); by-value struct member names are part of the CRC. modpost reads CRCs only from `#SYMVER` lines, so forge via a `__GENKSYMS__`-only stand-in declaration.
9. Write-only static globals are deleted by GlobalOpt unless the stores are `WRITE_ONCE`.
10. `trace_printk` creates two statics per site (`trace_printk_fmt` and the `trace_puts` one, both `.N` uniqued); a helper inlined twice shares one.

## Tools
`work/scripts/perfC/`: `ck.sh mist thin [func]`, `pb.sh mist`, `vsearch.py` (copies include/ subdirs now), `nd.sh`, `ckll.sh`, `crcforge.py`, `gks2.sh` (on the server, `<build-dir>`).
