# mca_strategy_fg_class - STATUS: EQUIVALENT

Template: XRING `mca/mca_strategy/strategy_fg/mca_strategy_fg_class.c`.  The stock module is newer (27 exports, in this order):
- removed `get_rawsoc`, `get_recharge`, `get_high_temp_vterm` (not in the stock);
- added `get_thermal_temperature`, `dual_is_chip_ok(int)`, `get_soh`, `get_first_termination`, `get_pack_vendor_id`, `get_temp_offset_flag`, (wrapper
  pattern identical to the other functions); order = stock address order.
- header `stubs_mcaB/include/mca/strategy/strategy_fg_class.h`: added `struct strategy_fg_class_info {void *data; struct strategy_fg_class_ops *ops;}` and the
  27 prototypes (ops struct was already reconstructed from the BTF in the RE run).

Result: offline genksyms 27/27 export CRCs ok (no forcing needed); real build + compare_ko: 29 functions identical, relocations, objects (`g_stg_fg_class_info.0/.1`),
27 exports with CRCs, 3 imports, modinfo -> EQUIVALENT (first build).
