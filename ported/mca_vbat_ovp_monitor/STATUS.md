# mca_vbat_ovp_monitor - EQUIVALENT (reference = OS4 stock `mca_vbat_ovp_monitor.ko`)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_hardware_monitor/mca_vbat_ovp_monitor.c` + `inc/mca_vbat_ovp_monitor.h` (header flattened into the package).
Differences stock(OS3 == OS4 binary) vs template, taken from the disassembly:
- `mca_vbat_mon_get_vbat_ovp_status`: only a single (non-series) battery is supported: `if (info->fg_type > MCA_FG_TYPE_SINGLE) return 0;` (template: `> MCA_FG_TYPE_SINGLE_NUM_MAX`).
- `mca_vbat_ovp_monitor_workfunc`: the fake-ovp test is a single `fake_vbat_for_debug > 4600` (the series branch `> 9200` of the template does not exist); this also removes 7 source lines, which makes all `__LINE__` immediates of the log calls match.
Reconstructed public headers extended (`stubs_a17B/include`): `platform_class_buckchg_ops_get_online`, `platform_class_wireless_is_present` (`int(unsigned role, int *)`), `SYSFS_DEV_5 "hw_monitor"`, `CountryCN` (hwid.h copied from stubs_mcaA).
Providers = generated OS4 stubs `//re/stubs_a17B/os4:os4_*` (exact OS4 export CRCs via forged genksyms struct tags, `work/a17/portB/gen_stubs.py`). ThinLTO defconfig + `-ffunction-sections`.

Result vs OS4 stock (compare_ko): 8 functions byte-identical, relocations, objects, 0 exports, 27 imports + CRCs, modinfo -> EQUIVALENT.
