# mca_business_battery_comp - EQUIVALENT

`compare_ko.py`: function set (15), bytes, resolved relocations, global objects, exports (0), imports+CRC (35), modinfo: all OK.
Data objects (`business_battery_driver`, `match_table`, `_batt_psy_desc`, `batt_psy_props`, `psy_tzd_ops`, `g_business_battery_log_ops`...): identical.
Only vermagic/intree/scmversion/built_with differ (build environment); `depends` identical.

Source = the XRING `mca_business_battery.c` + `mca_battery_psy.c` (stock module = both objects) with the Qualcomm-variant changes recovered from the binary:
- `get_prop_batt_capacity_level`: the "Trigger Android shutdown" message is `mca_log_err` (XRING: info);
- new `power_supply_read_temp()` (thermal zone ops, `strategy_class_fg_ops_get_thermal_temperature()` * 100, `printk` "batt_thermal temp:%d" only when the value changed, function-static
  `last_temp`) + non-const `psy_tzd_ops` + `psy_register_thermal()` (copy of the power-supply core helper: `psy->tzd = thermal_tripless_zone_device_register(desc->name, psy, &psy_tzd_ops, &tzp)`
  with a zeroed `struct thermal_zone_params`), called from `battery_psy_init()` after the successful `devm_power_supply_register()`;
- `_batt_psy_desc.no_thermal = true` (so the core does not register its own thermal zone);
- `business_battery_dump_log_context()` reads the five properties BEFORE the `!info` check and has no `!info->batt_psy` test.
Build: ThinLTO via `defconfig` (see AGENT_BRIEF), `#line` pins for the five log lines of `business_battery_psy_init`/`battery_psy_init` (410, 467, 473, 477),
two extra include lines in mca_business_battery.c keep the other line numbers of the XRING file (95/112/122/129).
Providers (stubs in `work/re/src/stubs_mcaA`): mca_log (+charge_log_register), mca_event, mca_parse_dts (all exports), mca_common (votable), mca_strategy_fg_class (13 getters),
mca_platform_buckchg_class (get_chg_type). CRCs were verified offline with `<build-dir> <stub.c>` (genksyms) against the stock exporters.
