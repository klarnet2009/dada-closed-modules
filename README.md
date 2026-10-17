# dada-closed-modules

Reverse-engineered and ported source code for the closed-source kernel modules of the **Xiaomi 15 (codename `dada`, Snapdragon 8 Elite / SM8750)**.

Xiaomi's stock kernel for this phone loads roughly 630 GPL-licensed modules, but for many of them no public source exists. That makes it hard to build custom ROMs and kernels with full control over the device. This repository collects what we could recover:

- **`reconstructed/`**: 26 modules that have no public source in any place we looked (all 267 branches of Xiaomi's public MiCode repositories, the OnePlus and LineageOS Qualcomm trees). We rebuilt their C source from the stock `.ko` binaries (first from HyperOS 3.0.6, `OS3.0.6.0.WOCMIXM`, then re-checked and corrected against HyperOS 4.0, `OS4.0.0.6.XOCCNXM`: the table says which) and compiled them against a GKI 6.6.144 tree with ThinLTO. **15 are EQUIVALENT to the stock binary, 11 are PARTIAL.**
- **`ported/`**: 101 modules whose source exists in older public Xiaomi trees (MiCode `*-oss` branches, XRING `xiaomi-modules`, OnePlus). We ported them to 6.6.144 and verified them the same way (82 EQUIVALENT, 17 PARTIAL, 2 COMPILES-only). Original copyright headers are kept. The table below says which stock binaries each one was compared against: the first round against HyperOS 3.0.6, the second round (101 modules) against the HyperOS 4.0 (Android 17) stock modules.
- **`docs/blocked/`**: the notes (`STATUS.md`) of the modules we could not get to compile yet, with what exactly is missing (usually headers or definitions that exist in no public tree).
- **`stubs/`**: tiny stand-in provider modules used only to satisfy export/import CRCs when a module is built on its own.
- **`tools/`**: `compare_ko.py` (the equivalence check), `crctool.py`, `crc_patch.py`, `btf_dump.py`, `btf_dump_startid.py`, `gen_stubs.py` (stub providers with exact export CRCs), `padcrc.py`, `verify_pkg.py` and `preamble_cmp.py` (audits that `compare_ko.py` does not do: kCFI type hashes and the modinfo `depends=` line, see below), plus `payload_extract.py` and `vendor_boot_extract.py` (stdlib-only extractors for OTA `payload.bin` and `vendor_boot` images).
- **`docs/`**: the method (`METHOD.md`), the lessons of the second round (`LESSONS_round2.md`) and the source map (`source-map/`): which module has source where, and which ones were found nowhere.

## Read this first

- **Nothing here has been tested on hardware.** "Equivalent" is a binary-level comparison, not a functional test.
- **Do not put the charging and fuel-gauge modules on a real battery** (the reconstructed `mca_*`, `mca_business_*`, `mca_strategy_fg_comp`, and the ported `mca_*`, `bq27z561`, `nuvolta_1652`, ...) without verifying them on hardware first. Use the stock binaries.
- This is reverse-engineered code. Some modules contain documented build hacks (below).
- Not affiliated with, or endorsed by, Xiaomi or Qualcomm.

## What the statuses mean

`tools/compare_ko.py stock.ko built.ko` compares function bodies, relocations, objects, exports, imports with CRCs, and modinfo.

- **EQUIVALENT**: all of the above match the stock module.
- **PARTIAL**: same logic and interfaces, but some functions differ in register allocation or block layout (for example `mi_damon` 187/188 functions byte-identical, `lb` 42/46). Per-module counts are in each `STATUS.md`.
- **COMPILES-only**: the module builds against 6.6.144, but only part of the stock functions match or exist yet (`STATUS.md` lists which ones are missing).
- **BLOCKED**: does not compile yet; see `docs/blocked/<module>.md`.

Known hacks, documented per module:

- `mist` and `miloadtrace` use forged export/import CRCs (stand-ins for what `genksyms` would produce).
- `tas25xx` needs the stock export CRCs applied afterwards with `tools/crc_patch.py`.
- A few functions carry codegen hints (`likely`/`unlikely`, dead literals) only to reproduce the stock instruction sequence.

## Reconstructed modules (no public source anywhere)

| module | status | compared against |
|---|---|---|
| cameralog | PARTIAL | HyperOS 4.0 |
| cameramsger | PARTIAL | HyperOS 4.0 |
| debug_ext | EQUIVALENT | HyperOS 4.0 |
| lb | PARTIAL | HyperOS 4.0 |
| mca_adsp_glink | EQUIVALENT | HyperOS 4.0 |
| mca_business_battery_comp | EQUIVALENT | HyperOS 4.0 |
| mca_business_charger_comp | PARTIAL | HyperOS 4.0 |
| mca_business_misc_comp | EQUIVALENT | HyperOS 4.0 |
| mca_qcom_panel | EQUIVALENT | HyperOS 4.0 |
| mca_qcom_smem | EQUIVALENT | HyperOS 4.0 |
| mca_qcom_subpmic_proxy | EQUIVALENT | HyperOS 4.0 |
| mca_qcom_sysfs | EQUIVALENT | HyperOS 4.0 |
| mca_strategy_fg_comp | PARTIAL | HyperOS 4.0 |
| mi_damon | PARTIAL | HyperOS 4.0 |
| mi_perf_memory | EQUIVALENT | HyperOS 4.0 |
| mi_schedule | PARTIAL | HyperOS 4.0 |
| mibbr | PARTIAL | HyperOS 4.0 |
| millet_oem_cgroup | EQUIVALENT | HyperOS 4.0 |
| miloadtrace | PARTIAL | HyperOS 4.0 |
| mist | PARTIAL | HyperOS 4.0 |
| process_monitor | PARTIAL | HyperOS 4.0 |
| qcom_adsp_pd_protocol | EQUIVALENT | HyperOS 4.0 |
| scene_swappiness | EQUIVALENT | HyperOS 4.0 |
| sia91xx_tuning | EQUIVALENT | HyperOS 4.0 |
| tas25xx | EQUIVALENT (stock export CRCs applied with tools/crc_patch.py) | HyperOS 3.0.6 |
| typec_analog_acc | EQUIVALENT | HyperOS 4.0 |

## Ported modules

| module | status | compared against |
|---|---|---|
| aw882xx_dlkm | COMPILES-only | HyperOS 4.0 |
| binder_gki | PARTIAL | HyperOS 4.0 |
| bootinfo | EQUIVALENT | HyperOS 4.0 |
| charger_partition | EQUIVALENT | HyperOS 4.0 |
| cs35l43_dlkm | EQUIVALENT | HyperOS 4.0 |
| dcvs_main | PARTIAL | HyperOS 4.0 |
| dump_binder | PARTIAL | HyperOS 4.0 |
| dynamic_readahead | EQUIVALENT | HyperOS 4.0 |
| fault_report | PARTIAL | HyperOS 4.0 |
| fs19xx_dlkm | EQUIVALENT | HyperOS 4.0 |
| gsim | EQUIVALENT | HyperOS 4.0 |
| hardwareinfo | EQUIVALENT | HyperOS 4.0 |
| hl7603 | EQUIVALENT | HyperOS 4.0 |
| hwid | EQUIVALENT | HyperOS 4.0 |
| mca_bmd | EQUIVALENT | HyperOS 4.0 |
| mca_charge_interface | EQUIVALENT | HyperOS 4.0 |
| mca_charge_mievent | EQUIVALENT | HyperOS 4.0 |
| mca_charger_thermal | EQUIVALENT | HyperOS 4.0 |
| mca_common | EQUIVALENT | HyperOS 4.0 |
| mca_event | EQUIVALENT | HyperOS 4.0 |
| mca_hwid | EQUIVALENT | HyperOS 4.0 |
| mca_ibat_ocp_monitor | EQUIVALENT | HyperOS 4.0 |
| mca_log | EQUIVALENT | HyperOS 4.0 |
| mca_lpd_detect | EQUIVALENT | HyperOS 4.0 |
| mca_parse_dts | EQUIVALENT | HyperOS 4.0 |
| mca_path_control | EQUIVALENT | HyperOS 4.0 |
| mca_pd_auth | EQUIVALENT | HyperOS 4.0 |
| mca_platform_base | EQUIVALENT | HyperOS 4.0 |
| mca_platform_bc12_class | EQUIVALENT | HyperOS 4.0 |
| mca_platform_buckchg_class | EQUIVALENT | HyperOS 4.0 |
| mca_platform_cp_class | EQUIVALENT | HyperOS 4.0 |
| mca_platform_fg_ic_ops | EQUIVALENT | HyperOS 4.0 |
| mca_platform_loadsw_class | EQUIVALENT | HyperOS 4.0 |
| mca_platform_wireless_class | EQUIVALENT | HyperOS 4.0 |
| mca_protocol_class | EQUIVALENT | HyperOS 4.0 |
| mca_protocol_pd_class | EQUIVALENT | HyperOS 4.0 |
| mca_protocol_qc_class | EQUIVALENT | HyperOS 4.0 |
| mca_pwm_fan | PARTIAL | HyperOS 4.0 |
| mca_soc_limit | EQUIVALENT | HyperOS 4.0 |
| mca_strategy_class | EQUIVALENT | HyperOS 4.0 |
| mca_strategy_fg_class | EQUIVALENT | HyperOS 4.0 |
| mca_sysfs | EQUIVALENT | HyperOS 4.0 |
| mca_vbat_ovp_monitor | EQUIVALENT | HyperOS 4.0 |
| mca_wireless_revchg | PARTIAL | HyperOS 4.0 |
| mca_workqueue | EQUIVALENT | HyperOS 4.0 |
| mi_fp | EQUIVALENT | HyperOS 4.0 |
| mi_low_lat_mem | PARTIAL | HyperOS 4.0 |
| mi_memory | EQUIVALENT | HyperOS 4.0 |
| mi_mempool | EQUIVALENT | HyperOS 4.0 |
| mi_power | EQUIVALENT | HyperOS 4.0 |
| mi_reclaim | PARTIAL | HyperOS 4.0 |
| mi_stack | EQUIVALENT | HyperOS 4.0 |
| mi_thermal_interface | EQUIVALENT | HyperOS 4.0 |
| mi_trace | EQUIVALENT | HyperOS 4.0 |
| mi_ubt | EQUIVALENT | HyperOS 4.0 |
| mi_ubt_test | EQUIVALENT | HyperOS 4.0 |
| mi_wmark | EQUIVALENT | HyperOS 4.0 |
| mi_wq_dynamic_priority | EQUIVALENT | HyperOS 4.0 |
| miev | EQUIVALENT | HyperOS 4.0 |
| mii | EQUIVALENT | HyperOS 4.0 |
| miicmpfilter | EQUIVALENT | HyperOS 4.0 |
| millet_binder | EQUIVALENT | HyperOS 4.0 |
| millet_core | EQUIVALENT | HyperOS 4.0 |
| millet_hs | EQUIVALENT | HyperOS 4.0 |
| millet_pkg | EQUIVALENT | HyperOS 4.0 |
| millet_sig | EQUIVALENT | HyperOS 4.0 |
| minet | EQUIVALENT | HyperOS 4.0 |
| miscore | PARTIAL | HyperOS 4.0 |
| nuvolta_1652 | EQUIVALENT | HyperOS 4.0 |
| nxp-nci | EQUIVALENT | HyperOS 4.0 |
| perfC_mst | PARTIAL | HyperOS 4.0 |
| perf_actuator | EQUIVALENT | HyperOS 4.0 |
| perfmgr | EQUIVALENT | HyperOS 4.0 |
| plat_common | EQUIVALENT | HyperOS 4.0 |
| rsmc_driver | PARTIAL | HyperOS 4.0 |
| rss_monitor | PARTIAL | HyperOS 4.0 |
| rtg | EQUIVALENT | HyperOS 4.0 |
| sc8581_sy6541 | EQUIVALENT | HyperOS 4.0 |
| sched-penalty | EQUIVALENT | HyperOS 4.0 |
| sched_entity_freq | PARTIAL | HyperOS 4.0 |
| si_core_module | EQUIVALENT | HyperOS 4.0 |
| sia91xx_dlkm | COMPILES-only | HyperOS 4.0 |
| sia91xx_tuning_dlkm | EQUIVALENT | HyperOS 4.0 |
| sla | EQUIVALENT | HyperOS 4.0 |
| stm_console | EQUIVALENT | HyperOS 4.0 |
| stm_core | EQUIVALENT | HyperOS 4.0 |
| stm_ftrace | EQUIVALENT | HyperOS 4.0 |
| stm_heartbeat | EQUIVALENT | HyperOS 4.0 |
| stm_nfc_i2c | EQUIVALENT | HyperOS 4.0 |
| stm_p_ost | EQUIVALENT | HyperOS 4.0 |
| stm_st54se_gpio | EQUIVALENT | HyperOS 4.0 |
| synaptics_tcm2 | PARTIAL | HyperOS 4.0 |
| tango32 | EQUIVALENT | HyperOS 4.0 |
| typec_analog_acc_dlkm | EQUIVALENT | HyperOS 4.0 |
| ufs_ffu | PARTIAL | HyperOS 4.0 |
| vip_sched | EQUIVALENT | HyperOS 4.0 |
| xiaomi_touch | EQUIVALENT | HyperOS 4.0 |
| xiaomi_wifi_gpio | EQUIVALENT | HyperOS 4.0 |
| xm_power | EQUIVALENT | HyperOS 4.0 |
| xr_qi | PARTIAL | HyperOS 4.0 |
| xr_qs | PARTIAL | HyperOS 4.0 |

Modules marked as compared against HyperOS 3.0.6 only were not re-checked against the HyperOS 4.0 binaries yet: for those, the OS4 stock module may differ (in the first OS3/OS4 check of 20 shared modules, 13 were identical, 7 changed, 6 disappeared). Treat them as a starting point, not as verified for OS4.

Modules still blocked: `bq27z561`, `mca_basic_wireless`, `mca_buckchg_jeita`, `mca_connector_antiburn`, `mca_quick_wireless`, `mca_smart_charge`, `mca_strategy_buckchg`, `mca_strategy_quickchg`, `tfa98xx_dlkm` (details in `docs/blocked/`).

## Building

Each module directory has a `BUILD.bazel` (Kleaf DDK) and, where needed, a `<module>_defconfig` that enables ThinLTO. Labels use the layout we built with, `//re/<name>`: merge `reconstructed/*`, `ported/*` and `stubs/*` into one `re/` directory of a GKI 6.6 Kleaf tree. Exact commands and the lessons learned are in `docs/METHOD.md`.

## HyperOS 4.0 (Android 17) status

The first sources were written against the HyperOS 3.0.6 binaries. A function-level comparison with the HyperOS 4.0 beta modules (`OS4.0.0.6.XOCCNXM`) of the 20 modules both versions share showed:

- 13 are identical in OS4.
- 7 changed: `mca_qcom_smem`, `mca_qcom_subpmic_proxy`, `mca_business_charger_comp`, `mca_strategy_fg_comp`, `mibbr`, `lb`, `scene_swappiness`.
- 6 disappeared in OS4: `mi_perf_memory`, `mi_schedule`, `miloadtrace`, `typec_analog_acc`, `tas25xx`, `sia91xx_tuning`.
- 12 are new in OS4: `dump_binder`, `dynamic_readahead`, `fault_report`, `matt`, `mca_pwm_fan`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `mi_wq_dynamic_priority`, `miscore`, `perf_actuator`, `plat_common`.

Since then the reconstructed and the ported modules were re-built and corrected against the OS4 binaries (the "compared against" column of the tables). Most of the new OS4 modules now have a source too; some are still PARTIAL, and `matt`, `mi_game` and `mitee_dlkm` are not written yet. Modules that disappeared in OS4 have no counterpart to compare with; their logic moved into other modules (for example `mi_perf_memory` into `mi_reclaim`). The stock OS4 modules are CRC-compatible with a 6.6.144 kernel (638 of 638 checked), so the stock binary can be used for every module that has no equivalent source yet.

### What `compare_ko.py` does not check

Two things matter when you want to load a rebuilt module, and `tools/compare_ko.py` does not compare them: the **kCFI type hash**, a 4-byte word that sits just before each function symbol (a mismatch makes the kernel trap at the first indirect call), and the **`depends=`** line of the modinfo. `tools/verify_pkg.py` and `tools/preamble_cmp.py` check both, plus vermagic, CRCs against `Module.symvers` and module signatures. A concrete case: our earlier `binder_gki` build passed `compare_ko` (9 of 10 functions) but its probe for `android_vh_binder_alloc_new_buf_locked` had a different kCFI hash than the kernel expects, so the stock module is the safe one there. Module signing: unsigned modules load like the stock vendor ones unless they export protected symbols; GKI `system_dlkm` modules are signed with the kernel build key.

## License and provenance

The code is licensed **GPL-2.0** (see `LICENSE`). The original modules are GPL-compatible licensed kernel modules shipped by Xiaomi (their modinfo declares `GPL`, `GPL v2` or `Dual BSD/GPL`). Reconstructed sources are derived from those binaries; ported sources derive from Xiaomi's public kernel source releases and keep their original copyright headers. If you are a rights holder and see a problem, please open an issue.

Contributions welcome: hardware test reports, fixes for PARTIAL modules, and sources for the modules still missing (see `docs/source-map/`).
