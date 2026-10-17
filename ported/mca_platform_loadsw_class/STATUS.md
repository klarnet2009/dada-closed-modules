# mca_platform_loadsw_class - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 13 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_platform_loadsw_class.ko`: compare_ko.py EQUIVALENT (13 functions, 6 exports, 16 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_platform_loadsw_class - STATUS: EQUIVALENT


Template: XRING `mca/mca_platform/mca_platform_loadsw_class.c`.  Only change vs the template: in `loadsw_sysfs_show` case LOADSW_PROP_IBAT_LIMIT the value is printed with
`val.intval` (the XRING source prints `val.boolval`; the stock reads the full int: `ldr w3,[sp]` instead of `ldrb`).
Header `stubs_mcaB/include/mca/platform/platform_loadsw_class.h` rewritten: `enum loadsw_role` {MASTER=MAIN=0, SLAVE, MAX}, `struct platform_class_loadsw_ops` (5 callbacks,
40 bytes, stock BTF) and 6 prototypes.  Deps: stub providers mca_log + mca_parse_dts, REAL mca_sysfs.

Result: offline genksyms 6/6 export CRCs ok (first try); real build + compare_ko: 13 functions identical, relocations, objects, 6 exports with CRCs, 16 imports, modinfo ->
EQUIVALENT (2 builds).
