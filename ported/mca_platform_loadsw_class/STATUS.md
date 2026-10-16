# mca_platform_loadsw_class - STATUS: EQUIVALENT

Template: XRING `mca/mca_platform/mca_platform_loadsw_class.c`.  Only change vs the template: in `loadsw_sysfs_show` case LOADSW_PROP_IBAT_LIMIT the value is printed with
`val.intval` (the XRING source prints `val.boolval`; the stock reads the full int: `ldr w3,[sp]` instead of `ldrb`).
Header `stubs_mcaB/include/mca/platform/platform_loadsw_class.h` rewritten: `enum loadsw_role` {MASTER=MAIN=0, SLAVE, MAX}, `struct platform_class_loadsw_ops` (5 callbacks,
40 bytes, stock BTF) and 6 prototypes.  Deps: stub providers mca_log + mca_parse_dts, REAL mca_sysfs.

Result: offline genksyms 6/6 export CRCs ok (first try); real build + compare_ko: 13 functions identical, relocations, objects, 6 exports with CRCs, 16 imports, modinfo ->
EQUIVALENT (2 builds).
