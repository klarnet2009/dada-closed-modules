# dada-closed-modules

Reverse-engineered and ported source code for the closed-source kernel modules of the **Xiaomi 15 (codename `dada`, Snapdragon 8 Elite / SM8750)**.

Xiaomi's stock kernel for this phone loads roughly 630 GPL-licensed modules, but for many of them no public source exists. That makes it hard to build custom ROMs and kernels with full control over the device. This repository collects what we could recover:

- **`reconstructed/`**: 26 modules that have no public source in any place we looked (all 267 branches of Xiaomi's public MiCode repositories, the OnePlus and LineageOS Qualcomm trees). We rebuilt their C source from the stock `.ko` binaries (HyperOS 3.0.6, `OS3.0.6.0.WOCMIXM`) and compiled them against a GKI 6.6.144 tree with ThinLTO. **15 are EQUIVALENT to the stock binary, 11 are PARTIAL.**
- **`ported/`**: 50 modules whose source exists in older public Xiaomi trees (MiCode `*-oss` branches, XRING `xiaomi-modules`). We ported them to 6.6.144 and verified them the same way (50 EQUIVALENT). Original copyright headers are kept.
- **`stubs/`**: tiny stand-in provider modules used only to satisfy export/import CRCs when a module is built on its own.
- **`tools/`**: `compare_ko.py` (the equivalence check), `crctool.py`, `crc_patch.py`, `btf_dump.py`, plus `payload_extract.py` and `vendor_boot_extract.py` (stdlib-only extractors for OTA `payload.bin` and `vendor_boot` images).
- **`docs/`**: the method (`METHOD.md`) and the source map (`source-map/`): which module has source where, and which ones were found nowhere.

## Read this first

- **Nothing here has been tested on hardware.** "Equivalent" is a binary-level comparison, not a functional test.
- **Do not put the reconstructed charging modules (`mca_*`, `mca_business_*`, `mca_strategy_fg_comp`, ...) on a real battery** without verifying them on hardware first. Use the stock binaries.
- This is reverse-engineered code. Some modules contain documented build hacks (below).
- Not affiliated with, or endorsed by, Xiaomi or Qualcomm.

## What the statuses mean

`tools/compare_ko.py stock.ko built.ko` compares function bodies, relocations, objects, exports, imports with CRCs, and modinfo.

- **EQUIVALENT**: all of the above match the stock module.
- **PARTIAL**: same logic and interfaces, but some functions differ in register allocation or block layout (for example `mi_damon` 187/188 functions byte-identical, `lb` 42/46). Per-module counts are in each `STATUS.md`.

Known hacks, documented per module:

- `mist` and `miloadtrace` use forged export/import CRCs (stand-ins for what `genksyms` would produce).
- `tas25xx` needs the stock export CRCs applied afterwards with `tools/crc_patch.py`.
- A few functions carry codegen hints (`likely`/`unlikely`, dead literals) only to reproduce the stock instruction sequence.

## Reconstructed modules (no public source anywhere)

| module | status |
|---|---|
| cameralog | PARTIAL: 10/11 functions byte-identical |
| cameramsger | PARTIAL: 14/22 functions byte-identical |
| debug_ext | PARTIAL: 28/28 functions identical; only the import CRC of qcom_is_dma_buf_file differs (provider built on 6.6.57 headers) |
| lb | PARTIAL: 42/46 functions byte-identical |
| mca_adsp_glink | EQUIVALENT |
| mca_business_battery_comp | EQUIVALENT |
| mca_business_charger_comp | EQUIVALENT |
| mca_business_misc_comp | EQUIVALENT |
| mca_qcom_panel | EQUIVALENT |
| mca_qcom_smem | EQUIVALENT |
| mca_qcom_subpmic_proxy | EQUIVALENT |
| mca_qcom_sysfs | EQUIVALENT |
| mca_strategy_fg_comp | PARTIAL: 73/76 functions byte-identical |
| mi_damon | PARTIAL: 187/188 functions byte-identical |
| mi_perf_memory | EQUIVALENT |
| mi_schedule | PARTIAL: 90/100 functions byte-identical |
| mibbr | PARTIAL: 26/27 functions byte-identical |
| millet_oem_cgroup | EQUIVALENT |
| miloadtrace | PARTIAL: 74/83 functions byte-identical |
| mist | PARTIAL: 67/70 functions byte-identical |
| process_monitor | PARTIAL: 38/42 functions byte-identical |
| qcom_adsp_pd_protocol | EQUIVALENT |
| scene_swappiness | EQUIVALENT |
| sia91xx_tuning | EQUIVALENT |
| tas25xx | EQUIVALENT (stock export CRCs applied with tools/crc_patch.py) |
| typec_analog_acc | EQUIVALENT |

## Ported modules

| module | status |
|---|---|
| bootinfo | EQUIVALENT |
| charger_partition | EQUIVALENT |
| hardwareinfo | EQUIVALENT |
| hl7603 | EQUIVALENT |
| hwid | EQUIVALENT |
| mca_charge_interface | EQUIVALENT |
| mca_charge_mievent | EQUIVALENT |
| mca_common | EQUIVALENT |
| mca_event | EQUIVALENT |
| mca_hwid | EQUIVALENT |
| mca_log | EQUIVALENT |
| mca_lpd_detect | EQUIVALENT |
| mca_parse_dts | EQUIVALENT |
| mca_platform_base | EQUIVALENT |
| mca_platform_bc12_class | EQUIVALENT |
| mca_platform_buckchg_class | EQUIVALENT |
| mca_platform_cp_class | EQUIVALENT |
| mca_platform_fg_ic_ops | EQUIVALENT |
| mca_platform_loadsw_class | EQUIVALENT |
| mca_protocol_class | EQUIVALENT |
| mca_protocol_qc_class | EQUIVALENT |
| mca_strategy_class | EQUIVALENT |
| mca_strategy_fg_class | EQUIVALENT |
| mca_sysfs | EQUIVALENT |
| mca_workqueue | EQUIVALENT |
| mi_power | EQUIVALENT |
| mi_stack | EQUIVALENT |
| mi_trace | EQUIVALENT |
| mi_ubt | EQUIVALENT |
| mi_ubt_test | EQUIVALENT |
| mi_wmark | EQUIVALENT |
| mii | EQUIVALENT |
| miicmpfilter | EQUIVALENT |
| millet_binder | EQUIVALENT |
| millet_core | EQUIVALENT |
| millet_hs | EQUIVALENT |
| millet_pkg | EQUIVALENT |
| millet_sig | EQUIVALENT |
| rsmc_driver | EQUIVALENT |
| sc8581 | EQUIVALENT |
| sched-penalty | EQUIVALENT |
| si_core_module | EQUIVALENT |
| speed_touch | EQUIVALENT |
| stm_console | EQUIVALENT |
| stm_core | EQUIVALENT |
| stm_ftrace | EQUIVALENT |
| stm_heartbeat | EQUIVALENT |
| stm_p_ost | EQUIVALENT |
| xiaomi_wifi_gpio | EQUIVALENT |
| xm_power | EQUIVALENT |

## Building

Each module directory has a `BUILD.bazel` (Kleaf DDK) and, where needed, a `<module>_defconfig` that enables ThinLTO. Labels use the layout we built with, `//re/<name>`: merge `reconstructed/*`, `ported/*` and `stubs/*` into one `re/` directory of a GKI 6.6 Kleaf tree. Exact commands and the lessons learned are in `docs/METHOD.md`.

## HyperOS 4.0 (Android 17) status

These sources were written against the HyperOS 3.0.6 binaries. A function-level comparison with the HyperOS 4.0 beta modules (`OS4.0.0.6.XOCCNXM`) of the 20 modules both versions share shows:

- 13 are identical in OS4.
- 7 changed: `mca_qcom_smem`, `mca_qcom_subpmic_proxy`, `mca_business_charger_comp`, `mca_strategy_fg_comp`, `mibbr`, `lb`, `scene_swappiness`.
- 6 disappeared in OS4: `mi_perf_memory`, `mi_schedule`, `miloadtrace`, `typec_analog_acc`, `tas25xx`, `sia91xx_tuning`.
- 12 are new in OS4 and not reverse-engineered yet: `dump_binder`, `dynamic_readahead`, `fault_report`, `matt`, `mca_pwm_fan`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `mi_wq_dynamic_priority`, `miscore`, `perf_actuator`, `plat_common`.

The sources in this repository have not been updated for OS4. The stock OS4 modules are CRC-compatible with a 6.6.144 kernel (638 of 638 checked), so the stock binaries can be used until updated sources exist.

## License and provenance

The code is licensed **GPL-2.0** (see `LICENSE`). The original modules are GPL-compatible licensed kernel modules shipped by Xiaomi (their modinfo declares `GPL`, `GPL v2` or `Dual BSD/GPL`). Reconstructed sources are derived from those binaries; ported sources derive from Xiaomi's public kernel source releases and keep their original copyright headers. If you are a rights holder and see a problem, please open an issue.

Contributions welcome: hardware test reports, fixes for PARTIAL modules, and sources for the modules still missing (see `docs/source-map/`).
