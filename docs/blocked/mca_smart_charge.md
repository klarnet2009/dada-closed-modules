# mca_smart_charge - BLOCKED before compile (scaffold only; reference = OS4 stock `mca_smart_charge.ko`, 35 functions, 7 exports)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_smart_charge/mca_smart_charge.c` + `mca_smart_charge.h` (copied flattened; BUILD.bazel/defconfig generated with `work/a17/portB/mkbuild.py`; providers = generated OS4 stubs `//re/stubs_a17B/os4`).

OS3 -> OS4 stock: same 35-function set, but 29 functions differ in bytes and the export CRC of `mca_smartchg_if_ops_register` changed (0xf068973d -> 0x85e2f9d0): the `struct mca_smartchg_if_ops` of `smart_chg_class.h` was extended in OS4.
The OS3 layout from the stock BTF (8 callbacks, 80 bytes) is in `stubs_a17B/include/mca/smartchg/smart_chg_class.h`; the new OS4 members must still be found from the code. The other 6 export CRCs are unchanged.

What is missing for a first compile (13 distinct errors left):
- the partition structs `smart_basp_header` (9 named fields checksum/type/total_len/jeita_ffc_term_size/jeita_normal_term_size/wired_ffc_size/wired_normal_size/wls_ffc_size/wls_normal_size, probably 10 ints),
  `smart_batt_spec` (`step_size`, flexible `steps[]`), `smart_batt_spec_curve`, `smart_batt_jeita_term_para` (public header `mca/shared_memory/charger_partition_class.h`, absent from every source tree: derive the layouts from the
  stock code, e.g. the inlined `smart_charge_cal_checksum`/`handle_baa_data` in `smart_charge_file_close`);
- `CHARGE_DFX_SMART_NAVIGATION_{TRIGGERED,SOC_ERR}`, `CHARGE_DFX_SMART_ENDURANCE_{TRIGGERED,SOC_ERR}` (values from the `mca_charge_mievent_report` immediates);
- `MCA_EVENT_CHARGE_STATUS` -> `MCA_EVENT_TYPE_CHARGE_STATUS`; `mca_smartchg_set_scene` returns void in the template (my `smart_chg_class.h` says int).

The stock has NO standalone `smart_charge_cal_checksum`/`handle_baa_data`/`handle_mmap_data` (inlined); otherwise the template function set matches.
Expected effort: header reconstruction ~20 symbols, then function-by-function adaptation of ~29 functions.
