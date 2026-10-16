# stm_core - EQUIVALENT (compare_ko.py all OK: 54 functions, 9 exports, 73 imports)
Template: micode_bixi `drivers/hwtracing/stm/{core.c,policy.c,stm.h}` (upstream Linux STM class). Source unchanged except link-layout emulation (no functional change):
- stock `.bss` is merged: `stm_register_device.__key[.1]` share address with `stm_source_srcu`, `stm_core_init.__key` with `stp_configfs_init.__key.llvm.*`. Emulated with
  `portA_lockkeys.h` (`PA_MUTEX_INIT(mutex, section)`) + `section(".bss.pa_keyA/B")` on stm_source_srcu.
- ThinLTO-promoted statics of the stock (global hidden): `stp_policy_subsys.llvm.17355142646671600349`, `.str.llvm.17355142646671600349` (= "&stp_policy_subsys.su_mutex", the lockdep name of mutex_init in
  stp_configfs_init) and `stp_configfs_init.__key.llvm.17355142646671600349`; defined with `__asm__` names in policy.c (mutex_init expanded by hand via __mutex_init).
