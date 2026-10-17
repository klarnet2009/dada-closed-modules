# mca_sysfs - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 14 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_sysfs.ko`: compare_ko.py EQUIVALENT (14 functions, 8 exports, 35 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_sysfs - STATUS: EQUIVALENT


Template: XRING `mca/mca_common/mca_sysfs.c`, source logic unchanged (14 functions incl. init/exit identical to the stock).
Changes vs the template:
- include list: `<linux/debugfs.h>` BEFORE `<mca/common/mca_sysfs.h>` (this is the include set that makes genksyms reproduce the stock export CRCs: the CRC of a
  prototype containing `struct device`/`struct attribute_group` depends on which nested struct definitions the TU has seen when the prototype is parsed;
  found earlier for the stub provider), then the headers the code needs (module, slab, mutex, seq_file, uaccess) AFTER the header so they cannot change the CRCs.
- header `stubs_mcaB/include/mca/common/mca_sysfs.h`: added `struct mca_sysfs_class_node` (layout from the stock BTF: node, class_name, mca_class, dev_list_header)
  and `struct mca_sysfs_dev_node` {node, name, dev} (inferred from the code, size/offsets confirmed by identical function bytes).

Result: offline genksyms 8/8 export CRCs ok; real build + compare_ko: 14 functions identical, relocations, objects, 8 exports with CRCs, 35 imports with CRCs, modinfo -> EQUIVALENT.
Lesson: the first template build had 7 CRC mismatches only because the include set differed; moving one `#include` before the public header fixed all.
