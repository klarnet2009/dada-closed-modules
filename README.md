# dada-closed-modules

Reverse-engineered and ported source code for the closed-source kernel modules of the **Xiaomi 15 (codename `dada`, Snapdragon 8 Elite / SM8750)**.

Xiaomi's stock kernel for this phone loads roughly 630 GPL-licensed modules, but for many of them no public source exists. That makes it hard to build custom ROMs and kernels with full control over the device. This repository collects what we could recover. It is a research artifact, not a drop-in replacement for the stock modules: read "Read this first" before you load anything.

## Read this first

- **Nothing here has been tested on a device.** Every claim below is a static, binary-level comparison of a module built from these sources against the stock `.ko`. "Equivalent" is not "works".
- **Do not load or flash these modules (use the stock binaries): `matt`, `mi_game`, `mi_low_lat_mem`, `xr_qi`, `xr_qs`, `aw882xx_dlkm`, `sia91xx_dlkm`, `tfa98xx_dlkm`, and the whole charging / fuel-gauge family** (reconstructed and ported `mca_*`, `mca_business_*`, `mca_strategy_fg_comp`, `mca_connector_antiburn`, `mca_wireless_revchg`, `mca_buckchg_jeita`, `mca_pwm_fan`, `bq27z561`, `nuvolta_1652`, `sc8581_sy6541`, `hl7603`, `charger_partition`, ...). The charging family is published for the community to read and review only: a wrong charging or gauge driver can damage a battery. Reviews that found real behaviour differences in it are in `docs/reviews/`. `sia91xx_tuning_dlkm` is a different module from `sia91xx_dlkm`.
- The old comparison tool (`tools/compare_ko.py`) has blind spots: it does not look at static data, tables, bug-table lines or the kCFI type hash in front of each function. The strict tool is `tools/compare_ko_v2.py` together with `tools/preamble_cmp.py` (kCFI); see `docs/VERIFICATION_METHODS.md`. Of the 100 modules marked EQUIVALENT, 99 were re-checked strictly (last column of the tables), the rest (1) only with the old tool.
- This is reverse-engineered code. Several modules contain documented build hacks (forged export/import CRCs, stub providers, `#line` pins, codegen hints): `docs/HACKS_AND_FORGED_CRCS.md`.
- Not affiliated with, or endorsed by, Xiaomi or Qualcomm.

## Status in numbers

145 modules are tracked in this repository (state 18.10.2026):

| status | modules |
|---|---|
| EQUIVALENT | 100 |
| PARTIAL | 35 |
| COMPILES-only | 3 |
| BLOCKED | 6 |
| not started (analysis only) | 1 (`mitee_dlkm`) |

(3 modules appear twice, once under their HyperOS 3 name and once under their HyperOS 4 name: `tas25xx`, `typec_analog_acc`, `sia91xx_tuning`.)

What the statuses mean (all against the stock module of HyperOS 4.0 where the table says so):

- **EQUIVALENT**: function bodies, relocations, exported/imported symbols with CRCs and modinfo match. Where the strict column says "strict-equivalent" or "differs in metadata only", `tools/compare_ko_v2.py` also compared the content of every data object and table (the remaining differences are `intree=`/`scmversion=`/`built_with=DDK` in modinfo, the LTO note section, `depends=` and, in a few modules, line numbers in the bug table).
- **PARTIAL**: same interfaces and logic as far as checked, but some functions differ (register allocation, block layout, and in some modules real code differences that the STATUS.md of the module lists). Per-module counts of identical functions are in each `STATUS.md`.
- **COMPILES-only**: builds against 6.6.144 and the set of functions equals the stock one, but many function bodies differ; not a usable replacement.
- **BLOCKED**: does not compile yet; see `docs/blocked/<module>.md`.

## What is in the repository

- **`reconstructed/`**: 26 modules that have no public source in any place we looked (all 267 branches of Xiaomi's public MiCode repositories, the OnePlus and LineageOS Qualcomm trees). Their C source was rebuilt from the stock `.ko` binaries (first from HyperOS 3.0.6, `OS3.0.6.0.WOCMIXM`, then re-checked and corrected against HyperOS 4.0, `OS4.0.0.6.XOCCNXM`: the table says which) and compiled against a GKI 6.6.144 tree with ThinLTO.
- **`ported/`**: 112 modules whose source exists in older public Xiaomi trees (MiCode `*-oss` branches, XRING `xiaomi-modules`, OnePlus), or that were written from scratch from the binary (for example `matt`, `mi_game`, `qca_cld3_*`). Ported to 6.6.144 and compared the same way (85 EQUIVALENT, 24 PARTIAL, 3 COMPILES-only). Original copyright headers are kept. 111 of them were compared against the HyperOS 4.0 (Android 17) stock modules.
- **`docs/blocked/`**: the notes (`STATUS.md`) of the modules that do not compile yet, with what exactly is missing (usually headers or definitions that exist in no public tree).
- **`stubs/`**: tiny stand-in provider modules and header sets used only to satisfy export/import CRCs when a module is built on its own. They are not modules to load.
- **`tools/`**: `compare_ko.py` (first-generation check), **`compare_ko_v2.py`** (strict check), `preamble_cmp.py` and `verify_pkg.py` (kCFI hashes, `depends=`, vermagic, CRCs against `Module.symvers`, load order), `crctool.py`, `crc_patch.py`, `crc_patch2.py`, `modinfo_fix.py`, `btf_dump.py`, `btf_dump_startid.py`, `patch_btf.py`, `gen_stubs.py` (stub providers with exact export CRCs), `padcrc.py`/`crcpad.py`, `gen_trace.py`, `payload_extract.py`, `vendor_boot_extract.py`, and the sub-directories `review/` (function fingerprints, data/table comparison), `matt_review/` (register-agnostic comparison of one module, kCFI checks), `multiset/` (instruction multiset diff, unity build) and `kcfi/` (kCFI hash to prototype search). Several scripts were written for our own build server and contain placeholders such as `<work-dir>` where a path was; they document the method more than they run unchanged.
- **`docs/`**: `STATUS_AND_LIMITS.md` (what is and is not proven), `VERIFICATION_METHODS.md` (compare_ko_v2, kCFI, multiset diff, BTF), `HACKS_AND_FORGED_CRCS.md`, `KERNEL_PACKAGE.md` (what a full module set built from this work looks like, in numbers), `reviews/` (independent reviews of the charging family, `matt` and the multiset audit), `METHOD.md`, `LESSONS_round2.md` and `source-map/` (which module has source where).

## Reconstructed modules (no public source anywhere)

| module | status | compared against | strict check |
|---|---|---|---|
| cameralog | PARTIAL | HyperOS 4.0 | - |
| cameramsger | PARTIAL | HyperOS 4.0 | - |
| debug_ext | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| lb | PARTIAL | HyperOS 4.0 | - |
| mca_adsp_glink | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_business_battery_comp | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_business_charger_comp | PARTIAL | HyperOS 4.0 | - |
| mca_business_misc_comp | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_qcom_panel | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_qcom_smem | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_qcom_subpmic_proxy | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_qcom_sysfs | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_strategy_fg_comp | PARTIAL | HyperOS 4.0 | - |
| mi_damon | PARTIAL | HyperOS 4.0 | - |
| mi_perf_memory | EQUIVALENT | HyperOS 3.0.6 only (no OS4 counterpart) | old check only |
| mi_schedule | PARTIAL | HyperOS 3.0.6 only (no OS4 counterpart) | - |
| mibbr | PARTIAL | HyperOS 4.0 | - |
| millet_oem_cgroup | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| miloadtrace | PARTIAL | HyperOS 3.0.6 only (no OS4 counterpart) | - |
| mist | PARTIAL | HyperOS 4.0 | - |
| process_monitor | PARTIAL | HyperOS 4.0 | - |
| qcom_adsp_pd_protocol | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| scene_swappiness | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sia91xx_tuning | EQUIVALENT | HyperOS 3.0.6 name (OS4: `sia91xx_tuning_dlkm`) | differs in metadata only |
| tas25xx | EQUIVALENT | HyperOS 3.0.6 name (OS4: `tas25xx_dlkm`) | equivalent after crc_patch2.py |
| typec_analog_acc | EQUIVALENT | HyperOS 3.0.6 name (OS4: `typec_analog_acc_dlkm`) | strict-equivalent |

## Ported modules

| module | status | compared against | strict check |
|---|---|---|---|
| aw882xx_dlkm | COMPILES-only | HyperOS 4.0 | - |
| binder_gki | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| bootinfo | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| charger_partition | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| cs35l43_dlkm | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| dcvs_main | PARTIAL | HyperOS 4.0 | - |
| dump_binder | PARTIAL | HyperOS 4.0 | - |
| dynamic_readahead | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| fault_report | PARTIAL | HyperOS 4.0 | - |
| fs19xx_dlkm | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| gsim | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| hardwareinfo | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| hl7603 | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| hwid | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| matt | PARTIAL | HyperOS 4.0 | - |
| mca_bmd | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_buckchg_jeita | PARTIAL | HyperOS 4.0 | - |
| mca_charge_interface | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_charge_mievent | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_charger_thermal | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_common | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_connector_antiburn | PARTIAL | HyperOS 4.0 | - |
| mca_event | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_hwid | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_ibat_ocp_monitor | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_log | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_lpd_detect | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_parse_dts | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_path_control | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_pd_auth | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_base | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_bc12_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_buckchg_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_cp_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_fg_ic_ops | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_loadsw_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_platform_wireless_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_protocol_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_protocol_pd_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_protocol_qc_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_pwm_fan | PARTIAL | HyperOS 4.0 | - |
| mca_soc_limit | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_strategy_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_strategy_fg_class | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_sysfs | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_vbat_ovp_monitor | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mca_wireless_revchg | PARTIAL | HyperOS 4.0 | - |
| mca_workqueue | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_fp | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_game | PARTIAL | HyperOS 4.0 | - |
| mi_low_lat_mem | PARTIAL | HyperOS 4.0 | - |
| mi_memory | EQUIVALENT | HyperOS 4.0 | differs in bug-table line numbers only |
| mi_mempool | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_power | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_reclaim | PARTIAL | HyperOS 4.0 | - |
| mi_stack | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_thermal_interface | EQUIVALENT | HyperOS 4.0 | differs in bug-table line numbers only |
| mi_trace | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_ubt | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_ubt_test | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_wmark | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mi_wq_dynamic_priority | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| miev | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| mii | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| miicmpfilter | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| millet_binder | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| millet_core | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| millet_hs | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| millet_pkg | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| millet_sig | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| minet | PARTIAL | HyperOS 4.0 | - |
| minidump | EQUIVALENT | HyperOS 3.0.6 | equivalent after crc_patch2.py |
| miscore | PARTIAL | HyperOS 4.0 | - |
| nuvolta_1652 | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| nxp-nci | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| perf_actuator | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| perfmgr | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| plat_common | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| qca_cld3_kiwi_v2 | PARTIAL | HyperOS 4.0 | - |
| qca_cld3_peach | PARTIAL | HyperOS 4.0 | - |
| qca_cld3_peach_v2 | PARTIAL | HyperOS 4.0 | - |
| qca_cld3_qca6750 | PARTIAL | HyperOS 4.0 | - |
| qca_cld3_wcn7750 | PARTIAL | HyperOS 4.0 | - |
| rsmc_driver | PARTIAL | HyperOS 4.0 | - |
| rss_monitor | PARTIAL | HyperOS 4.0 | - |
| rtg | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sc8581_sy6541 | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sched-penalty | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sched_entity_freq | PARTIAL | HyperOS 4.0 | - |
| si_core_module | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sia91xx_dlkm | COMPILES-only | HyperOS 4.0 | - |
| sia91xx_tuning_dlkm | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| sla | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_console | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_core | EQUIVALENT | HyperOS 4.0 | differs in bug-table line numbers only |
| stm_ftrace | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_heartbeat | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_nfc_i2c | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_p_ost | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| stm_st54se_gpio | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| synaptics_tcm2 | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| tango32 | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| tas25xx_dlkm | EQUIVALENT | HyperOS 4.0 | equivalent after crc_patch2.py |
| tfa98xx_dlkm | COMPILES-only | HyperOS 4.0 | - |
| typec_analog_acc_dlkm | EQUIVALENT | HyperOS 4.0 | strict-equivalent |
| ufs_ffu | PARTIAL | HyperOS 4.0 | - |
| vip_sched | EQUIVALENT | HyperOS 4.0 | differs in bug-table line numbers only |
| xiaomi_touch | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| xiaomi_wifi_gpio | EQUIVALENT | HyperOS 4.0 | differs in bug-table line numbers only |
| xm_power | EQUIVALENT | HyperOS 4.0 | differs in metadata only |
| xr_qi | PARTIAL | HyperOS 4.0 | - |
| xr_qs | PARTIAL | HyperOS 4.0 | - |

Modules marked as compared against HyperOS 3.0.6 only were not re-checked against the HyperOS 4.0 binaries: for those, the OS4 stock module may differ (in the first OS3/OS4 check of 20 shared modules, 13 were identical, 7 changed, 6 disappeared). Treat them as a starting point.

Modules still blocked: `bq27z561`, `mca_basic_wireless`, `mca_quick_wireless`, `mca_smart_charge`, `mca_strategy_buckchg`, `mca_strategy_quickchg` (details in `docs/blocked/`).

## Building

Each module directory has a `BUILD.bazel` (Kleaf DDK) and, where needed, a `<module>_defconfig` that enables ThinLTO. Labels use the layout we built with, `//re/<name>`: merge `reconstructed/*`, `ported/*` and `stubs/*` into one `re/` directory of a GKI 6.6 Kleaf tree. Exact commands and the lessons learned are in `docs/METHOD.md`. Third-party firmware arrays are not published (`ported/nuvolta_1652/inc/FIRMWARE_NOT_INCLUDED.txt`; `ufs_ffu` has zero-filled placeholders): to build those modules you have to supply them from your own stock modules.

## HyperOS 4.0 (Android 17) notes

The first sources were written against the HyperOS 3.0.6 binaries. A function-level comparison with the HyperOS 4.0 beta modules (`OS4.0.0.6.XOCCNXM`) of the 20 modules both versions share showed: 13 identical, 7 changed (`mca_qcom_smem`, `mca_qcom_subpmic_proxy`, `mca_business_charger_comp`, `mca_strategy_fg_comp`, `mibbr`, `lb`, `scene_swappiness`), 6 disappeared (`mi_perf_memory`, `mi_schedule`, `miloadtrace`, `typec_analog_acc`, `tas25xx`, `sia91xx_tuning`), 12 are new (`dump_binder`, `dynamic_readahead`, `fault_report`, `matt`, `mca_pwm_fan`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `mi_wq_dynamic_priority`, `miscore`, `perf_actuator`, `plat_common`). Since then the sources were rebuilt and corrected against the OS4 binaries (the "compared against" column). The stock OS4 modules are CRC-compatible with a 6.6.144 kernel (638 of 638 checked), so the stock binary can be used for every module that has no equivalent source yet.

## Kernel package result

Putting the verified modules together with the stock ones for a 6.6.144 kernel gives a 640-module set in which 52 modules are built from our sources, 18 from public kernel sources, 96 are the kernel's own build and 474 are the unchanged stock modules (the package itself is not published: it contains stock binaries). Details and the verification result are in `docs/KERNEL_PACKAGE.md`.

## License and provenance

The code is licensed **GPL-2.0** (see `LICENSE`). The original modules are GPL-compatible licensed kernel modules shipped by Xiaomi (their modinfo declares `GPL`, `GPL v2` or `Dual BSD/GPL`). Reconstructed sources are derived from those binaries; ported sources derive from Xiaomi's public kernel source releases and keep their original copyright headers. If you are a rights holder and see a problem, please open an issue.

Contributions welcome: hardware test reports, fixes for PARTIAL modules, independent reviews, and sources for the modules still missing (see `docs/source-map/`).
