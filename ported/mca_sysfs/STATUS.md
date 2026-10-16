# mca_sysfs - STATUS: EQUIVALENT

Template: XRING `mca/mca_common/mca_sysfs.c`, source logic unchanged (14 functions incl. init/exit identical to the stock).
Changes vs the template:
- include list: `<linux/debugfs.h>` BEFORE `<mca/common/mca_sysfs.h>` (this is the include set that makes genksyms reproduce the stock export CRCs: the CRC of a
  prototype containing `struct device`/`struct attribute_group` depends on which nested struct definitions the TU has seen when the prototype is parsed;
  found earlier for the stub provider), then the headers the code needs (module, slab, mutex, seq_file, uaccess) AFTER the header so they cannot change the CRCs.
- header `stubs_mcaB/include/mca/common/mca_sysfs.h`: added `struct mca_sysfs_class_node` (layout from the stock BTF: node, class_name, mca_class, dev_list_header)
  and `struct mca_sysfs_dev_node` {node, name, dev} (inferred from the code, size/offsets confirmed by identical function bytes).

Result: offline genksyms 8/8 export CRCs ok; real build + compare_ko: 14 functions identical, relocations, objects, 8 exports with CRCs, 35 imports with CRCs, modinfo -> EQUIVALENT.
Lesson: the first template build had 7 CRC mismatches only because the include set differed; moving one `#include` before the public header fixed all.
