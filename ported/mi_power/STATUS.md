# mi_power - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 -> OS4 stock differ in 2 of 54 functions (cxvt_state_show, cxvt_stats_sysfs_print): the CX vote array grew from 28 to 29 entries (new constant MAX_CX_DRV=29 in mi_power.c, the DDR vote array keeps MAX_DRV=28; the define replaces a blank line, so __LINE__ values are untouched). Rest identical (sections, exports, imports). Real Kleaf build vs OS4 stock vendor_ramdisk/mi_power.ko: compare_ko.py EQUIVALENT (54 functions, 13 exports, 57 imports).

Earlier headline (HyperOS 3 round): mi_power - EQUIVALENT


`compare_ko.py`: 54 functions (bytes+relocs), objects, 13 export CRCs, 57 import CRCs, modinfo: all OK. `work/scripts/portC_data.py`: 41 data objects identical, kCFI words identical (54); rodata strings identical.

Template: MiCode branch `goku-u-oss-test`, `drivers/misc/mi-power/mi_power.{c,h}` (1639 lines, Qualcomm flavour; zeus-s-oss / bsp-vermeer-t-oss are older and smaller). Same source family as the stock; the template is "the stock minus a few Xiaomi additions".
Changes against the template (all derived from the stock disassembly):
- additions: `temp_aware` class attribute (u8 `temp_aware_mode = 10`, `%d`, kstrtoull, accepts 0..9), `llcvt_state` attribute + `register_mi_llc_stats_get_ss_vote_info` (exported) + `llcvt_stats_sysfs_print` (global),
  PM notifier `pi_pm_notify` (SUSPEND_PREPARE/POST_SUSPEND: ktime_get / ktime_get_boottime, "[xiaomi_power]suspend exit at boottime %lld ms, AP total resume time ..."), `register_pm_notifier(&pi_pm_nb)` in init;
  attribute table order: wakeup_device_list, debug_suspend, power_mode, temp_aware, subsystem_sleep_state, cxvt_state, ddrvt_state, llcvt_state;
- `power_debug_enable` starts at 3 (gpio + regulator printing on), `wakeup_devices_show` uses `ssize_t written`;
- API drift 6.6.144: `class_create(name)` (no THIS_MODULE), class attribute callbacks take `const struct class *` / `const struct class_attribute *`, `qcom_scm_set_download_mode(QCOM_DOWNLOAD_FULLDUMP)` has ONE argument
  (header `linux/firmware/qcom/qcom_scm.h`), `"%s"` for the 4-char `stat_type`, `%ld` year in the CST print macro (tm_year is long), `%lx` for power_debug_enable; dropped unused includes (`gpiolib.h`...);
- dead GKI_HOOK pointers (`leaf_irqs`, `wakeup_reason_lock`... never assigned) fold `log_irq_wakeup_reason()` into a single printk exactly like the stock.
Quirks reproduced: `IS_ERR(power_debug_class)` tests a variable that is always NULL, `class_destroy(NULL)` on the error path, `power_mode_show` uses `0 < x < N`.
Stubs (`re/stubs_portC`, names match `depends=`): `qcom-scm` (`qcom_scm_set_download_mode`), `clk-qcom` (`clk_debug_suspend_atomic_enable`, `clock_debug_print_enabled_clocks`), `pinctrl-msm` (`debug_pintctrl_msm_{enable,disable}`),
`debug-regulator` (`regulator_debug_print_enable_regulators`) + read-only `//re/stubs:smem_stub` (`CONFIG_QCOM_SMEM=1`).
`clock_debug_print_enabled_clocks(struct seq_file *)`: the stock CRC expands `struct seq_file` of the 6.6.57 headers, not reproducible with 6.6.144 -> CRC forged with `work/scripts/perfC/crcforge.py` (only the stub, i.e. the provider side).

Lessons found here (reusable):
1. MMIO trace headers: 6.6.57 `asm-generic/io.h` calls `log_read_mmio()/log_post_read_mmio()` unconditionally, 6.6.144 guards them with `tracepoint_enabled()` (imports `__tracepoint_rwmmio_read` instead of `log_read_mmio`).
   Reproduce the stock (out-of-line `readl_relaxed`/`readq` local functions calling `log_*_mmio`, imports `log_read_mmio`/`log_post_read_mmio`) by including `<linux/io.h>` FIRST inside
   `#pragma push_macro("tracepoint_enabled") / #undef / #define tracepoint_enabled(tp) true / #include <linux/io.h> / #pragma pop_macro` (see head of mi_power.c).
2. genksyms does NOT expand a `struct` whose definition is in the .c file itself (only header-defined structs are expanded): the export CRC of `register_mi_llc_stats_get_ss_vote_info(int (*)(struct llc_island_stats_active_scids *))`
   matched the stock only after moving the struct definition from the local header into the .c (cx/ddr structs live in the header and are expanded). The same struct exists in the tree's `include/soc/qcom/qcom_stats.h` (6.6.144 has `__packed`, the stock layout is unpacked).
3. kCFI hash of a function pointer type gives the struct tag: `<work-dir>` (C brute force of xxh64 over `_ZTSFiP<len><name>E`, 25M names/s; source kept in work/scripts/kbrute.c) found `llc_island_stats_active_scids` after Python brute force failed; the real header names (grep the dada kernel tree) were the better hint.
