# dada-closed-modules

Reverse-engineered and ported source code for the closed-source kernel modules of the **Xiaomi 15 (codename `dada`, Snapdragon 8 Elite / SM8750)**.

Xiaomi's stock kernel for this phone loads roughly 630 GPL-licensed modules, but for many of them no public source exists. That makes it hard to build custom ROMs and kernels with full control over the device. This repository collects what we could recover:

- **`reconstructed/`**: 26 modules that have no public source in any place we looked (all 267 branches of Xiaomi's public MiCode repositories, the OnePlus and LineageOS Qualcomm trees). We rebuilt their C source from the stock `.ko` binaries (HyperOS 3.0.6, `OS3.0.6.0.WOCMIXM`) and compiled them against a GKI 6.6.144 tree with ThinLTO. **16 are EQUIVALENT to the stock binary, 10 are PARTIAL.**
- **`ported/`**: 98 modules whose source exists in older public Xiaomi trees (MiCode `*-oss` branches, XRING `xiaomi-modules`, OnePlus). We ported them to 6.6.144 and verified them the same way (81 EQUIVALENT, 15 PARTIAL, 2 COMPILES-only). Original copyright headers are kept. The table below says which stock binaries each one was compared against: the first round against HyperOS 3.0.6, the second round (47 modules) against the HyperOS 4.0 (Android 17) stock modules.
- **`docs/blocked/`**: the notes (`STATUS.md`) of the modules we could not get to compile yet, with what exactly is missing (usually headers or definitions that exist in no public tree).
- **`stubs/`**: tiny stand-in provider modules used only to satisfy export/import CRCs when a module is built on its own.
- **`tools/`**: `compare_ko.py` (the equivalence check), `crctool.py`, `crc_patch.py`, `btf_dump.py`, `btf_dump_startid.py`, `gen_stubs.py` (stub providers with exact export CRCs), `padcrc.py`, plus `payload_extract.py` and `vendor_boot_extract.py` (stdlib-only extractors for OTA `payload.bin` and `vendor_boot` images).
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
| mca_adsp_glink | EQUIVALENT | HyperOS 3.0.6 |
| mca_business_battery_comp | EQUIVALENT | HyperOS 3.0.6 |
| mca_business_charger_comp | EQUIVALENT | HyperOS 3.0.6 |
| mca_business_misc_comp | EQUIVALENT | HyperOS 3.0.6 |
| mca_qcom_panel | EQUIVALENT | HyperOS 3.0.6 |
| mca_qcom_smem | EQUIVALENT | HyperOS 3.0.6 |
| mca_qcom_subpmic_proxy | EQUIVALENT | HyperOS 3.0.6 |
| mca_qcom_sysfs | EQUIVALENT | HyperOS 3.0.6 |
| mca_strategy_fg_comp | PARTIAL | HyperOS 4.0 |
| mi_damon | PARTIAL | HyperOS 4.0 |
| mi_perf_memory | EQUIVALENT | HyperOS 3.0.6 |
| mi_schedule | PARTIAL: 90/100 functions byte-identical | HyperOS 3.0.6 |
| mibbr | PARTIAL | HyperOS 4.0 |
| millet_oem_cgroup | EQUIVALENT | HyperOS 3.0.6 |
| miloadtrace | PARTIAL: 74/83 functions byte-identical | HyperOS 3.0.6 |
| mist | PARTIAL | HyperOS 4.0 |
| process_monitor | PARTIAL | HyperOS 4.0 |
| qcom_adsp_pd_protocol | EQUIVALENT | HyperOS 3.0.6 |
| scene_swappiness | EQUIVALENT | HyperOS 3.0.6 |
| sia91xx_tuning | EQUIVALENT | HyperOS 3.0.6 |
| tas25xx | EQUIVALENT (stock export CRCs applied with tools/crc_patch.py) | HyperOS 3.0.6 |
| typec_analog_acc | EQUIVALENT | HyperOS 3.0.6 |

## Ported modules

| module | status | compared against |
|---|---|---|
| aw882xx_dlkm | COMPILES-only | HyperOS 4.0 |
| binder_gki | PARTIAL | HyperOS 4.0 |
| bootinfo | EQUIVALENT | HyperOS 3.0.6 |
| charger_partition | EQUIVALENT | HyperOS 3.0.6 |
| cs35l43_dlkm | EQUIVALENT | HyperOS 4.0 |
| dcvs_main | PARTIAL | HyperOS 4.0 |
| dump_binder | PARTIAL | HyperOS 4.0 |
| dynamic_readahead | EQUIVALENT | HyperOS 4.0 |
| fault_report | PARTIAL | HyperOS 4.0 |
| fs19xx_dlkm | PARTIAL | HyperOS 4.0 |
| gsim | EQUIVALENT | HyperOS 4.0 |
| hardwareinfo | EQUIVALENT | HyperOS 3.0.6 |
| hl7603 | EQUIVALENT | HyperOS 3.0.6 |
| hwid | EQUIVALENT | HyperOS 3.0.6 |
| mca_bmd | EQUIVALENT | HyperOS 4.0 |
| mca_charge_interface | EQUIVALENT | HyperOS 3.0.6 |
| mca_charge_mievent | EQUIVALENT | HyperOS 3.0.6 |
| mca_charger_thermal | EQUIVALENT | HyperOS 4.0 |
| mca_common | EQUIVALENT | HyperOS 3.0.6 |
| mca_event | EQUIVALENT | HyperOS 3.0.6 |
| mca_hwid | EQUIVALENT | HyperOS 3.0.6 |
| mca_ibat_ocp_monitor | EQUIVALENT | HyperOS 4.0 |
| mca_log | EQUIVALENT | HyperOS 3.0.6 |
| mca_lpd_detect | EQUIVALENT | HyperOS 3.0.6 |
| mca_parse_dts | EQUIVALENT | HyperOS 3.0.6 |
| mca_path_control | EQUIVALENT | HyperOS 4.0 |
| mca_pd_auth | EQUIVALENT | HyperOS 4.0 |
| mca_platform_base | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_bc12_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_buckchg_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_cp_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_fg_ic_ops | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_loadsw_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_platform_wireless_class | EQUIVALENT | HyperOS 4.0 |
| mca_protocol_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_protocol_pd_class | EQUIVALENT | HyperOS 4.0 |
| mca_protocol_qc_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_pwm_fan | PARTIAL | HyperOS 4.0 |
| mca_soc_limit | EQUIVALENT | HyperOS 4.0 |
| mca_strategy_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_strategy_fg_class | EQUIVALENT | HyperOS 3.0.6 |
| mca_sysfs | EQUIVALENT | HyperOS 3.0.6 |
| mca_vbat_ovp_monitor | EQUIVALENT | HyperOS 4.0 |
| mca_wireless_revchg | PARTIAL | HyperOS 4.0 |
| mca_workqueue | EQUIVALENT | HyperOS 3.0.6 |
| mi_fp | EQUIVALENT | HyperOS 4.0 |
| mi_memory | EQUIVALENT | HyperOS 4.0 |
| mi_mempool | PARTIAL | HyperOS 4.0 |
| mi_power | EQUIVALENT | HyperOS 3.0.6 |
| mi_stack | EQUIVALENT | HyperOS 3.0.6 |
| mi_thermal_interface | EQUIVALENT | HyperOS 4.0 |
| mi_trace | EQUIVALENT | HyperOS 3.0.6 |
| mi_ubt | EQUIVALENT | HyperOS 3.0.6 |
| mi_ubt_test | EQUIVALENT | HyperOS 3.0.6 |
| mi_wmark | EQUIVALENT | HyperOS 3.0.6 |
| mi_wq_dynamic_priority | EQUIVALENT | HyperOS 4.0 |
| miev | EQUIVALENT | HyperOS 4.0 |
| mii | EQUIVALENT | HyperOS 3.0.6 |
| miicmpfilter | EQUIVALENT | HyperOS 3.0.6 |
| millet_binder | EQUIVALENT | HyperOS 3.0.6 |
| millet_core | EQUIVALENT | HyperOS 3.0.6 |
| millet_hs | EQUIVALENT | HyperOS 3.0.6 |
| millet_pkg | EQUIVALENT | HyperOS 3.0.6 |
| millet_sig | EQUIVALENT | HyperOS 3.0.6 |
| minet | PARTIAL | HyperOS 4.0 |
| miscore | PARTIAL | HyperOS 4.0 |
| nxp-nci | EQUIVALENT | HyperOS 4.0 |
| perfC_mst | PARTIAL | HyperOS 3.0.6 |
| perf_actuator | EQUIVALENT | HyperOS 4.0 |
| perfmgr | EQUIVALENT | HyperOS 4.0 |
| plat_common | EQUIVALENT | HyperOS 4.0 |
| rsmc_driver | EQUIVALENT | HyperOS 3.0.6 |
| rss_monitor | PARTIAL | HyperOS 4.0 |
| rtg | EQUIVALENT | HyperOS 4.0 |
| sc8581 | EQUIVALENT | HyperOS 3.0.6 |
| sc8581_sy6541 | EQUIVALENT | HyperOS 4.0 |
| sched-penalty | EQUIVALENT | HyperOS 3.0.6 |
| sched_entity_freq | PARTIAL | HyperOS 4.0 |
| si_core_module | EQUIVALENT | HyperOS 3.0.6 |
| sia91xx_dlkm | COMPILES-only | HyperOS 4.0 |
| sia91xx_tuning_dlkm | EQUIVALENT | HyperOS 4.0 |
| sla | EQUIVALENT | HyperOS 4.0 |
| speed_touch | EQUIVALENT | HyperOS 3.0.6 |
| stm_console | EQUIVALENT | HyperOS 3.0.6 |
| stm_core | EQUIVALENT | HyperOS 3.0.6 |
| stm_ftrace | EQUIVALENT | HyperOS 3.0.6 |
| stm_heartbeat | EQUIVALENT | HyperOS 3.0.6 |
| stm_nfc_i2c | EQUIVALENT | HyperOS 4.0 |
| stm_p_ost | EQUIVALENT | HyperOS 3.0.6 |
| stm_st54se_gpio | EQUIVALENT | HyperOS 4.0 |
| tango32 | EQUIVALENT | HyperOS 4.0 |
| typec_analog_acc_dlkm | EQUIVALENT | HyperOS 4.0 |
| vip_sched | EQUIVALENT | HyperOS 4.0 |
| xiaomi_touch | EQUIVALENT | HyperOS 4.0 |
| xiaomi_wifi_gpio | EQUIVALENT | HyperOS 3.0.6 |
| xm_power | EQUIVALENT | HyperOS 3.0.6 |
| xr_qi | PARTIAL | HyperOS 4.0 |
| xr_qs | PARTIAL | HyperOS 4.0 |

Modules still blocked: `bq27z561`, `mca_basic_wireless`, `mca_buckchg_jeita`, `mca_connector_antiburn`, `mca_quick_wireless`, `mca_smart_charge`, `mca_strategy_buckchg`, `mca_strategy_quickchg`, `nuvolta_1652`, `synaptics_tcm2`, `tfa98xx_dlkm` (details in `docs/blocked/`).

## Building

Each module directory has a `BUILD.bazel` (Kleaf DDK) and, where needed, a `<module>_defconfig` that enables ThinLTO. Labels use the layout we built with, `//re/<name>`: merge `reconstructed/*`, `ported/*` and `stubs/*` into one `re/` directory of a GKI 6.6 Kleaf tree. Exact commands and the lessons learned are in `docs/METHOD.md`.

## HyperOS 4.0 (Android 17) status

These sources were written against the HyperOS 3.0.6 binaries. A function-level comparison with the HyperOS 4.0 beta modules (`OS4.0.0.6.XOCCNXM`) of the 20 modules both versions share shows:

- 13 are identical in OS4.
- 7 changed: `mca_qcom_smem`, `mca_qcom_subpmic_proxy`, `mca_business_charger_comp`, `mca_strategy_fg_comp`, `mibbr`, `lb`, `scene_swappiness`.
- 6 disappeared in OS4: `mi_perf_memory`, `mi_schedule`, `miloadtrace`, `typec_analog_acc`, `tas25xx`, `sia91xx_tuning`.
- 12 are new in OS4 and not reverse-engineered yet: `dump_binder`, `dynamic_readahead`, `fault_report`, `matt`, `mca_pwm_fan`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `mi_wq_dynamic_priority`, `miscore`, `perf_actuator`, `plat_common`.

The reconstructed sources have not been updated for OS4 (the ported modules of the second round, marked above, were built against OS4 directly). The stock OS4 modules are CRC-compatible with a 6.6.144 kernel (638 of 638 checked), so the stock binaries can be used until updated sources exist.

## License and provenance

The code is licensed **GPL-2.0** (see `LICENSE`). The original modules are GPL-compatible licensed kernel modules shipped by Xiaomi (their modinfo declares `GPL`, `GPL v2` or `Dual BSD/GPL`). Reconstructed sources are derived from those binaries; ported sources derive from Xiaomi's public kernel source releases and keep their original copyright headers. If you are a rights holder and see a problem, please open an issue.

Contributions welcome: hardware test reports, fixes for PARTIAL modules, and sources for the modules still missing (see `docs/source-map/`).
