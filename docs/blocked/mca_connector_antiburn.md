# mca_connector_antiburn - BLOCKED (template is XRING-specific, the dada stock has a different function set)

SAFETY: charging module (`mca_*`). Source is for the community only, never to be flashed or put into a ROM build.

Reference binary: `<work-dir>` (17 functions, 51 imports; the OS3 stock has 17 functions with only 7 identical), vermagic 6.6.57-android15-8.
Template: XRING `work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_connector_antiburn.c` (721 lines) + `inc/mca_connector_antiburn.h`, copied here. The template uses the XRING PMIC (`soc/xring/xr_pmic.h`, `xr_pmic_gpio_set_value`) and `platform_sc6601a_cid_class.h`, which the dada stock does not use.

The stock function set differs from the template: `connector_antiburn_check_status_v2` (static `not_attched_count`), `connector_antiburn_connect_cb`, `connector_antiburn_hw_cb` (mca_hwid based), `connector_antiburn_debug_notifier_cb`, `connector_antiburn_get_temp_increase_rate` (statics first_boot_flag/last_time/last_connector_temp), `connector_antiburn_ensure_vbus_sense5V` (920 bytes), `connector_antiburn_get_temperature` (adc_err_flag), `connector_antiburn_dump_log_head/_context` (charge log ops `g_connector_antiburn_log_ops`), `connector_antiburn_thermal_notifier_event`, a 3892-byte `connector_antiburn_monitor_workfunc`, exported `connector_antiburn_is_triggered`, global `antiburn_sysfs_field_tbl` (200 bytes), statics `flag_restore`, `g_conn`.
Imports new in OS4 vs OS3: `of_get_child_by_name`, `memset`, `strcmp`. Depends: mca_workqueue, mca_hwid, mca_protocol_pd_class, mca_protocol_class, mca_charge_mievent.
This is a rewrite from the disassembly (about 3100 instructions in `<work-dir>`), not a port; not started. `stubs_a17A/include/mca/hardware/hw_connector_antiburn.h` and the needed pd/qc protocol prototypes are available.

G2: not started (no change). The template includes `soc/xring/xr_pmic.h` (absent, XRING-only); the module has to be rewritten from the disassembly (see the function list above). `jt.py` (work/re/g2_tools) decodes its switch tables.
