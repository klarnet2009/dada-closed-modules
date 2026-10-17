# perf_actuator - EQUIVALENT (compare_ko.py all OK: 14 functions, 4 exports + CRC, 8 imports; 2 export CRCs forged, see below)
Reference: OS4 stock `vendor_ramdisk/perf_actuator.ko` (6.6.57). Kleaf DDK + ThinLTO defconfig (`g1_tools/bz.sh perf_actuator`). No public template (XRING-named: `xr_fbt`, `xr_perf_actuator`), reconstructed from the disassembly + BTF.
Content: two ioctl dispatch tables (`g_perf_actuator_table[42]`, `g_xr_fbt_table[12]`, entry = {u32 cmd, int (*func)(void *)}) behind two misc devices (`xr_perf_actuator`, `xr_fbt`), magic 'x'; consumers (rtg, vip_sched) register ioctl handlers via the 4 GPL exports.
Two source files (`pa_main.c` + `xr_fbt.c`; DDK refuses a single `perf_actuator.c` next to a second file with the same name as the module), `xr_fbt_dev_init/exit` are global, not exported.
Documented hacks:
- `xr_fbt_device` is a ThinLTO-promoted object in stock: `xr_fbt_device.llvm.11015404421372416167` (hidden, `__asm__` name) - hash is not reproducible.
- Export CRC of `register_perf_actuator` (0x024adecb) and `register_xr_fbt` (0x7ae74ca6): typedef names `PERF_ACTUATOR_FUNC` / `XR_FBT_FUNC` (`typedef int (*)(void *)`) taken from the stock BTF, but no spelling of the prototype reproduced the stock CRC (brute force over return/param types and typedef names failed), so `perf_actuator.h` has a GENKSYMS-ONLY pad parameter type in the typedef (forcecrc.py). The compiler never sees it (`#ifdef __GENKSYMS__`); `unregister_*` CRCs matched without any hack.
- kCFI hash of the table callback = `_ZTSFiPvE` (0x89fb613d), i.e. `int (*)(void *)`.
