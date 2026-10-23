# mca_strategy_fg_class - EQUIVALENT vs the GLOBAL OS4.0.0.7 stock (G14b 19.10.2026; real Kleaf: 33 of 33 functions byte-identical, kCFI, exports+CRC, imports, objects PASS; only buildmeta differs). COMMUNITY-ONLY: never flash, never in a ROM/kernel package; the charging family stays STOCK in any image.

## G14b (19.10.2026): updated from CN 0.6 to GLOBAL OS4.0.0.7.XOCMIXM
Change CN -> global: `struct strategy_fg_class_ops` grows 232 -> 240 bytes: new `+224 strategy_fg_set_dynamic_fv(void *data, int fv)`, `strategy_fg_set_allow_new_soc_smooth` moves +224 -> +232 (the only existing function that changed: `strategy_class_fg_set_allow_new_soc_smooth`, slot offset 0xe0 -> 0xe8); one new export `strategy_class_fg_set_dynamic_fv(int fv)` (same one-parameter wrapper pattern, -1 without ops/slot). `strategy_class_fg_ops_register` CRC 0x401e13a6 -> 0x944e1834 comes from the struct only (natural, no hack). Meaning of "dynamic_fv" INFERRED from the name/consumers, not proved. CN source kept as `*.cn06`.

(older entries below describe the CN OS4.0.0.6 state)

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 -> OS4 stock changed 18 of 29 functions: struct strategy_fg_class_ops grew from 26 to 29 callbacks / 232 bytes (new strategy_fg_get_comp_rm after get_rm at +88, get_comp_fcc after get_fcc at +176, set_allow_new_soc_smooth(void *data, int allow) at +224; every later member shifts, hence the 15 byte-different wrappers) and three new exports (strategy_class_fg_get_comp_rm(int *), get_comp_fcc(int *), set_allow_new_soc_smooth(int)) at stock address positions (comp_rm after get_rm, comp_fcc after get_fcc, set_allow_new_soc_smooth last). Layout from the OS4 BTF (START_ID 141904, g6/btf/mca_strategy_fg_class_os4.txt). The OS4 header is a local copy in the module dir (strategy_fg_class_os4.h, same include guard; the shared stubs_mcaB header keeps the OS3 layout). Real Kleaf build vs OS4 stock vendor_ramdisk/mca_strategy_fg_class.ko: compare_ko.py EQUIVALENT (32 functions, 30 exports incl. the changed register_ops CRC with no forging, 3 imports); data objects (g6/datacmp.py): identical. Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_strategy_fg_class - STATUS: EQUIVALENT


Template: XRING `mca/mca_strategy/strategy_fg/mca_strategy_fg_class.c`.  The stock module is newer (27 exports, in this order):
- removed `get_rawsoc`, `get_recharge`, `get_high_temp_vterm` (not in the stock);
- added `get_thermal_temperature`, `dual_is_chip_ok(int)`, `get_soh`, `get_first_termination`, `get_pack_vendor_id`, `get_temp_offset_flag`, (wrapper
  pattern identical to the other functions); order = stock address order.
- header `stubs_mcaB/include/mca/strategy/strategy_fg_class.h`: added `struct strategy_fg_class_info {void *data; struct strategy_fg_class_ops *ops;}` and the
  27 prototypes (ops struct was already reconstructed from the BTF in the RE run).

Result: offline genksyms 27/27 export CRCs ok (no forcing needed); real build + compare_ko: 29 functions identical, relocations, objects (`g_stg_fg_class_info.0/.1`),
27 exports with CRCs, 3 imports, modinfo -> EQUIVALENT (first build).
