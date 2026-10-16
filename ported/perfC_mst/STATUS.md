# mist (perfC) -- status: PARTIAL (59/70 functions byte-identical)

Build: Kleaf ddk_module, `mist_defconfig` = ThinLTO (`# CONFIG_LTO_NONE is not set` + `CONFIG_LTO_CLANG_THIN=y`),
`-ffunction-sections -fdata-sections`, `local_defines CONFIG_QCOM_DCVS/CONFIG_QCOM_PMU_LIB/CONFIG_QTI_SCMI_VENDOR_PROTOCOL`
(the GKI tree already ships `soc/qcom/dcvs.h` and `pmu_lib.h`; without these defines the headers give `static inline` -ENODEV stubs and the
compiler silently deletes the vote calls). 3 TUs as in stock: `mist_dflt.c` (DDR/LLCC/L3 latency tuner + proc/param glue + init/exit),
`mist_gflt.c` (GPU frame latency tuner), `mist_lc.c` (load-calibration glue). Imports are provided by `re/stubs_perfC`
(`qcom-pmu-lib`, `qcom-dcvs`, `clk-qcom` stubs, headers `soc/qcom/mist_ext.h`).

compare_ko result (real Bazel build): function set OK (70), global objects (name,size) OK, modinfo OK, imports+CRC OK (51),
exports+CRC 43 of 44 OK, function bytes: 59 of 70 identical.

## Still different
Functions (all semantically reconstructed; differences are layout/scheduling/regalloc):
- cpufreq_to_memfreq: stock keeps a second variable for the map slot and reloads `mist_map[hw][idx]` after the debug printk (mine forwards it).
- mist_set_next_freq_hook: stock hoists the `cluster[cpu]` lookup + local-bounds check of `mist_soc_freq_thresh[cid]` above the `util > cap>>3` test; mine sinks it.
- mist_update_rel_freq: only the order of the two UBSAN traps (cpu<=7 first, then &frame_stats_record[idx]) and load placement.
- mi_dflt_calculate_dflt_lantancy_hook: cold (debug==5) print blocks laid out in a different order (stock: spm, cycles, ftrace, ips, id0..id2).
- mist_dflt_init / parse_cluster_node_map: structure only partly matched (cleanup labels, loop shapes, cpumask atomics), not iterated to the end.
- mi_gpu_flt_working: stock tests `mi_soc_governor_enable` with `tbz w8,#0`; every C variant tried (bool/u8/volatile/__used) gives `cmp #1; b.ne` or `cbz`.
- mist_gflt_stop: stock has one shared `ret`; mine tail-duplicates it.
- gpu_load_calibration / gpubw_load_calibration: stock evaluates `busy > wait` first then `pct > thr` branchless (subs/ccmp/csel); mine reverses the compare order.
- commit_fast_vote: code identical; only the relocation target name differs: stock `commit_fast_vote.trace_printk_fmt.75`, mine `.70`
  (the `.N` suffix is clang's module-wide unique-name counter; the stock TU simply created 5 more uniqued names before that function).
Exports: `mi_soc_register_freq_adjhook` CRC 0x1bd27011 not reproduced (tried char */const char *, hook second arg
unsigned int/u32/ulong/..., enum spellings with/without `=N`/trailing comma, typedef before/after devfreq.h, ~1000 variants).

## Lessons (reusable)
- genksyms only expands struct bodies that are DEFINED at the EXPORT_SYMBOL point: for exports taking `struct devfreq *` / `struct cpufreq_policy *` the CRC depends on
  which headers the stock TU included. Bisect the include set with a real-flags genksyms (`<build-dir>`, same flags as the kernel compile,
  validated against Module.symvers and stock imports): gflt TU = only module/kernel/devfreq/workqueue (no cpufreq.h/of.h/seq_file.h), dflt TU needed `linux/interrupt.h`.
  Keep mist.h include-free and put per-TU include lists in the .c files.
- By-value struct members names are part of the CRC (`struct gflt_load_info {total_time, busy_time, ...}` found by brute force over harvested words, crc32 of the dump + ' ').
- `u32` vs `unsigned int`, `cpumask_t` vs `struct cpumask`, `struct hrtimer *` vs `void *` all change CRCs: brute force prototype spellings with genksyms (110-2000 candidates/min).
- UBSAN: `&arr[i]` (pointer taken) checks `i < N+1` (`cmp #N+1; b.hs`), `arr[i].member[j]` checks `i < N` and member arrays use `cmp #M-1; b.hi`.
- write-only static globals are deleted by GlobalOpt unless the stores are `WRITE_ONCE` (volatile): `mem_sample_counter`.
- `unlikely()` on debug flags moves the printk blocks out of line and changes spill placement (callee-saved vs cold spills): decide per call site by comparing the stock prologue
  (stock hook with callee-saved x19-x25 => plain `if`, stock with cold blocks => `unlikely`).
- `if (!a) return; if (!b) return;` vs `if (!a || !b) return;` changes bool tests (`cbz` vs `cmp #1`).
- `trace_printk` creates two statics per site (`trace_printk_fmt` and the `trace_puts` one, both `.N` uniqued) and a helper inlined twice shares one.
