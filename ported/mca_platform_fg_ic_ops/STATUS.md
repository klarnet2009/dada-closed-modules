# mca_platform_fg_ic_ops - STATUS: EQUIVALENT

Template: XRING `mca/mca_platform/mca_platform_fg_ic_ops.c` (757 lines of identical wrappers).  The stock module is newer: 66 exports (template: 64) in a
different order, many added (`get_batt_abnormal_info, get_original_temp, get_decimal(+rate), get_chg_vol, get_chip_ok, get_aged_flag, get_raw_soc, set_iterm, get_charge_status,
get_manufacturing_date, set/get_first_usage_date, get_calc_rvalue ...`), 6 removed (`get_max_cell_volt, get_soc_decimal, get_chg_voltage, set_co_mos, get_co_status,
get_chg_fet_status`).  Because the wrappers are mechanical the .c is generated from one table (`scripts_mcaB/gen_fgops.py`, stock address order) with the
template's exact wrapper pattern; everything outside the wrappers (header block, `struct fuelguage_info`, `platform_get_fg_ic_ops`, register, driver) is copied
from the template.  Quirk kept from the template and present in the stock: `get_count_level2` checks the *level1* callback and calls level2.

Header `stubs_mcaB/include/mca/platform/platform_fg_ic_ops.h` (also generated): `struct fuelguage_ic_ops` (536 bytes, 67 callbacks) from the stock BTF, 67 prototypes.
Note: `platform_fg_ops_get_first_usage_date/get_manufacturing_date` (`unsigned char *`) and `set_first_usage_date` (`const char *`) DO reproduce the stock CRCs with
plain spelling (brute force of typedef/array/pointer spellings in Python on the genksyms stream); the forced pads in `stub_mca_platform_fg_ic_ops.c` are therefore not needed.

Result: offline genksyms 67/67 ok; real build + compare_ko: 69 functions identical, relocations, objects, 67 exports with CRCs, 3 imports, modinfo -> EQUIVALENT
(2 builds: first had the level2 quirk missing).
