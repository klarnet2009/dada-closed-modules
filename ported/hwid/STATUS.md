# hwid - EQUIVALENT

`compare_ko.py`: 15 functions, bytes, relocations, global objects, 9 export CRCs, 9 import CRCs, modinfo: all OK. `work/scripts/portC_data.py`: 15 data objects identical, kCFI words identical, 36 switch-table pointers identical.

Template: MiCode `drivers/misc/hwid/hwid.c` (branches yunluo-s-oss, pearl-s-oss, yuechu-t-oss). That is the OLD hwid (params country/level/version/sku, exports get_hw_sku...), it is NOT the stock code.
The stock is a newer Qualcomm hwid (params `hwid_value`, `project`, `build_adc`, `project_adc`; imports `socinfo_get_id` from the qcom `socinfo` module; exports
`product_name_get` + 8 `get_hw_*` getters). Used from the template only: file header/style, `hwid_attr()` macro, kobject `hwid` + sysfs group init/exit (including its quirk that the success path falls through
into `kobject_del()`), module params with `module_param(x, uint, 0444)`, author/description/license strings.
Rewritten from the disassembly: `product_name_get()` (switch on socinfo id 457/519/557/614/618 then on `project`; the compiler turns it into 4 lookup tables 12/6/7/11 entries; id 556 is NOT a case -
adding it merges tables and changes the compare chain), getters (`hwid_value >> 20` country, low half minor, high half major, `(high half) & 0xf` build), 4 sysfs show functions ("0x%x\n" for project/value, "%d\n" for the adc values).
Export prototypes: all return `uint32_t` (matches CRCs), `product_name_get` returns `const char *`.
Stub: `re/stubs_portC:socinfo_stub` (`socinfo.ko`, exports `socinfo_get_id`, CRC matches stock; needs `CONFIG_QCOM_SOCINFO=1` in local_defines of the module and the stub).
Build: ThinLTO defconfig, `CONFIG_LTO=1`, `-ffunction-sections -fdata-sections`.
Gotcha: compare_ko does NOT look at compiler-generated lookup tables (switch tables without symbol) - the first build was "EQUIVALENT" with two wrong table entries; check with `portC_data.py`.
