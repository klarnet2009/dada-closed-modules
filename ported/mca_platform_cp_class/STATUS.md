# mca_platform_cp_class - STATUS: EQUIVALENT

Template: XRING `mca/mca_platform/mca_platform_cp_class.c` (984 lines).  The stock module is newer, so the 52 export wrappers are generated from one table in
stock address order (`scripts_mcaB/gen_cpclass.py`) with the template's wrapper pattern; the register function, `ibus_delta/total`, `set_fsw` (force_fsw logic), the dt parser,
the two sysfs tables/show/store, `cp_sysfs_create_group`, probe and driver are copied unchanged from the template.
Stock vs template: removed `set_default_mode`, `check_iic_check`; added `set_adjustadble_timeout, get_probe_ok (ops cp_get_probe_status), set_revchg, set_busovp, set_pmid2out_uvp_dis,
get_pmid2out_uvp_dis (bool BY VALUE, as the stock BTF says), set_manual_revchg_mode, set_cp_reverse_mode`; `CP_ROLE_MAX` is 3 (`cmp w0,#2; b.ls`).
Header `stubs_mcaB/include/mca/platform/platform_cp_class.h` (generated): `enum cp_role` {MASTER, SLAVE, THIRD, MAX}, `struct platform_class_cp_ops` (49 callbacks, 392 bytes,
order from the stock BTF) and 52 prototypes.  The BTF type of six callback arguments (T79) is unresolved; the CRC of `platform_class_cp_register_ops` is reproduced only
with `u32 *` (16 spellings tried: `unsigned int *`, `int *`, `u32 *` ...).
`__LINE__`: the stock logs carry line numbers of the original newer source (+13..+15 lines vs our layout in three regions); pinned with three `#line` directives (set_fsw,
dt parser .. sysfs show/store, sysfs group create .. probe) that the generator inserts.
Deps: stub providers mca_log + mca_parse_dts, REAL mca_sysfs (`EXTRA="mca_sysfs" push.sh`).

Result: offline genksyms 52/52; real build + compare_ko: 61 functions identical, relocations, objects, 52 exports with CRCs, 17 imports, modinfo -> EQUIVALENT
(builds: 1 CRC, 2 bytes (4 functions differed only by `__LINE__`), 3 EQUIVALENT).
