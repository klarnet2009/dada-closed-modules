# si_core_module - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 36 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/si_core_module.ko`: compare_ko.py EQUIVALENT (36 functions, 7 exports, 54 imports). Fix needed: the ThinLTO-promoted names changed their hash (OS4: `xa_si_objects.llvm.11199766761704967050`, `async_si_buffer.llvm.1634875710961754174`, `si_core_wq.llvm.4468286372555553306`; OS3 had 6182029828322236042 / 13117605046560047247 / 11952267189415990395), updated via PA_PROMOTED in si_core.c, si_core_async.c, si_core_wq.c (the only change).

Earlier headline (HyperOS 3 round): si_core_module - EQUIVALENT (compare_ko.py all OK: 36 functions, 7 exports, 54 imports)

Template: micode_bixi `drivers/firmware/qcom/si_core/{qcom_scm_invoke.c,si_core.c,si_core_async.c,si_core_adci.c,si_core_wq.c,*.h}` (= Qualcomm si_core), unchanged except naming of ThinLTO-promoted symbols
(`PA_PROMOTED` in stubs_portA/include/portA_thinlto.h): `xa_si_objects.llvm.6182029828322236042` (si_core.c), `async_si_buffer.llvm.13117605046560047247` (function, si_core_async.c), `si_core_wq.llvm.11952267189415990395` (si_core_wq.c).
Config: CONFIG_QCOM_SI_CORE=1, CONFIG_QCOM_SI_CORE_WQ=1, CONFIG_QSEECOM_PROXY=1 (the last one adds the `qseecom_process_listener_from_smcinvoke` bridge to si_object_do_invoke).
Providers (stub module //re/stubs_portA:si_core_stub, same header prototypes -> import CRCs match): qcom_scm_invoke_smc, qcom_scm_invoke_callback_response, qtee_shmbridge_allocate/free_shm, qseecom_process_listener_from_smcinvoke.
