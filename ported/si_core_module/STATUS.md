# si_core_module - EQUIVALENT (compare_ko.py all OK: 36 functions, 7 exports, 54 imports)
Template: micode_bixi `drivers/firmware/qcom/si_core/{qcom_scm_invoke.c,si_core.c,si_core_async.c,si_core_adci.c,si_core_wq.c,*.h}` (= Qualcomm si_core), unchanged except naming of ThinLTO-promoted symbols
(`PA_PROMOTED` in stubs_portA/include/portA_thinlto.h): `xa_si_objects.llvm.6182029828322236042` (si_core.c), `async_si_buffer.llvm.13117605046560047247` (function, si_core_async.c), `si_core_wq.llvm.11952267189415990395` (si_core_wq.c).
Config: CONFIG_QCOM_SI_CORE=1, CONFIG_QCOM_SI_CORE_WQ=1, CONFIG_QSEECOM_PROXY=1 (the last one adds the `qseecom_process_listener_from_smcinvoke` bridge to si_object_do_invoke).
Providers (stub module //re/stubs_portA:si_core_stub, same header prototypes -> import CRCs match): qcom_scm_invoke_smc, qcom_scm_invoke_callback_response, qtee_shmbridge_allocate/free_shm, qseecom_process_listener_from_smcinvoke.
