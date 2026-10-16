# mca_path_control - EQUIVALENT to the HyperOS 4.0 (OS4) stock module

SAFETY: charging module (`mca_*`). The source is for the community only; it must never be flashed or put into a ROM build.

Reference binary: `<work-dir>` (identical copy in vendor_dlkm), vermagic 6.6.57-android15-8. OS3 and OS4 stock have the same 13 functions byte for byte.
Template: XRING tree `work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_path_control.c` + `inc/mca_path_control.h` - used UNCHANGED (the XRING copy is the same source as the dada stock).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, `compare_ko.py`): EQUIVALENT = 13 functions byte-identical, relocations, global objects, 1 export (`mca_path_control_enable_gate`) + CRC, 41 imports + CRCs, modinfo.

What was missing (headers only, `work/re/src/stubs_a17A/include`):
- `mca/hardware/hw_path_control.h` (new): `enum path_control_src { NONE=0, USB=1, WLS=2, WLS_REV=4, OTG=8, VDD=16 }; typedef enum path_control_src CONTROL_SRC;` (taken from the BTF of the stock, `START_ID=141904 btf_dump.py local`), plus `enum otg_boost_enable_status` (1/2 = on; stock `(sts - 1) < 2`).
- `strategy_wireless_class.h`: `mca_wireless_rev_get_rev_boost_default(int *)`, `mca_wireless_rev_get_reverse_chg(bool *)`.
- Export CRC of `mca_path_control_enable_gate`: the real spelling of the enum (value spelling / unknown extra tokens) was not found by brute force (about 500 spellings tried), so the header has a GENKSYMS-ONLY pad enumerator (`#ifdef __GENKSYMS__ fcrcNqpwPuNl1L, #endif`, CRC32-forged with `a17A_scripts/padcrc.py`, same technique as stubs_mcaB). The compiler never sees it: layout and code are unaffected; only the export CRC (ABI check of consumers) is reproduced.
- Providers for the build: generated stubs `st_mca_wireless_revchg`, `st_mca_platform_wireless_class` (stubs_a17A, `gen_stub.py`), stubs_mcaA (log, parse_dts, cp_class, buckchg), real mca_sysfs.
Build: `bash work/re/src/a17_push.sh mca_path_control` then on the server `<work-dir> mca_path_control` (own stock dir `<work-dir>`).
