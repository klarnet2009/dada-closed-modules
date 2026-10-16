# mca_charge_interface - EQUIVALENT (agent mcaA, port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_charge_interface.c (unchanged)
Result: EQUIVALENT: compiles on 6.6.144; function set (7), bytes, relocs, export mca_charge_if_ops_register + CRC, imports + CRC (21), modinfo (depends mca_log,mca_sysfs,mca_event), 8 data objects identical

- the template source needed no change; the missing public header mca/common/mca_charge_interface.h was reconstructed (src/stubs_mcaA/include): struct mca_charge_if_ops (112 bytes from BTF, user params and type_name are const char * - proven by the kcfi hashes of the ops call sites: _ZTSFiPKcPcPvE and _ZTSFiPKcjPvE), MCA_CHARGE_IF_MAX_VALUE_BUFF = 128 (stack frame), 19 chg types + END
- tool: scratchpad kcfi_try.py brute-forces itanium-mangled function types against a kcfi hash (with S_ substitutions), see work/scripts/kcfi_hash.py
