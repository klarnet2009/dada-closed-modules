# mca_ibat_ocp_monitor - EQUIVALENT to the HyperOS 4.0 (OS4) stock module

SAFETY: charging module (`mca_*`). The source is for the community only; it must never be flashed or put into a ROM build.

Reference binary: `<work-dir>` (identical copy in vendor_dlkm), vermagic 6.6.57-android15-8.
Template: XRING tree `work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_ibat_ocp_monitor.c` + `inc/mca_ibat_ocp_monitor.h`
(the template file header says mca_vbat_ovp_monitor.c: it is the ibat copy of the vbat module).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, `compare_ko.py`): EQUIVALENT = 8 functions byte-identical (incl. `__LINE__`), relocations, global objects (ibat_ocp_sysfs_field_tbl), 0 exports, 27 imports with CRCs, modinfo.

Changes relative to the template (OS4 stock = newer source than the XRING copy):
- `FAST_HEARTBEAT_TIMER_MS` 2000, `NORMAL_HEARTBEAT_TIMER_MS` 5000 (template 10000/60000).
- `enum strategy_fg_type` without `MCA_FG_TYPE_SINGLE_SERIES` (PARALLEL == 1); the global `OCP_THR[]` array is replaced by `info->ocp_threshold[2]` (new struct member at +0xa8, struct is 0xb0 bytes), parsed from the DT property `ocp_threshold` (2 x u32), defaults OCP_THRESHOLD_MAINT/FLIP.
- `mca_ibat_ocp_mon_parse_dt()`: logs "parse fg_type failed", `ret |= mca_parse_dts_u32_array(np, "ocp_threshold", info->ocp_threshold, 2)`, on error sets the defaults and logs "parse ocp_threshold failed, use default value", always logs "ocp_threshold : %d %d" and `return ret` (probe then fails with "parse dt faile" -> -1).
- `mca_ibat_mon_get_ibat_ocp_status()`: new block `{ bool loadsw_present = true; platform_class_loadsw_get_present(LOADSW_ROLE_MASTER, &loadsw_present); if (!loadsw_present) mca_charge_mievent_report(<event 35>, NULL, 0); }` before the fast-charge check (the nested block scope is needed so that the stack slot is shared with the inlined master_curr, as in the stock). Event 35 is `CHARGE_DFX_LOAD_SWITCH_IIC_ERR` in our reconstructed header (the name is a guess, the value is from the code).
- Two extra includes (loadsw class, charge mievent) and a few blank lines so that every `__LINE__` immediate equals the stock.

Build / deps: `stubs_a17A` generated stub providers (`gen_stub.py` over the reconstructed headers: wireless class, strategy_fg_class, fg_ic_ops), stubs_mcaA (log, event, parse_dts, buckchg), stubs_mcaB (charge_mievent) and the real EQUIVALENT modules mca_sysfs and mca_platform_loadsw_class. All 27 import CRCs equal the stock.
Build: `bash work/re/src/a17_push.sh mca_ibat_ocp_monitor` then on the server `<work-dir> mca_ibat_ocp_monitor` (own stock dir `<work-dir>`).
