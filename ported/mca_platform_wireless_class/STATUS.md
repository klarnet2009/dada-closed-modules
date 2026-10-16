# mca_platform_wireless_class - EQUIVALENT (reference = OS4 stock `mca_platform_wireless_class.ko`)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_platform/mca_platform_wireless_class.c`. The source in this directory was started by the HyperOS 3 run (wrapper block + ops struct generated from the stock BTF by
`work/re/scripts_mcaB/gen_wlsclass.py`; header `stubs_a17B/include/mca/platform/platform_wireless_class.h`, 186 lines) but never finished/verified. The OS3 and OS4 binaries of this module are identical.
Done now: (1) the sysfs part adapted to the stock: the stock exposes only the 17 `rx_*`/`fw_version`/`wls_bin`/... attributes, the template's 13 `TX_SYSFS_*` attributes (tx_vout, tx_iout, tx_tdie, tx_ss, pen_*) and their show/store cases + enumerators do not exist
(removed); (2) `__LINE__` pinned by deleting two blank lines (the log lines of probe/show/store/create_group match); (3) kcfi hash of `wls_set_charge_type` identified as `int(u8, void *)`
(test compile of `u32,int,u8,u16,..`) and the genksyms spelling of the wrapper argument is `uint8_t` (CRC brute force over u8/unsigned char/uint8_t/__u8: only `uint8_t` gives 0x22de7e59);
(4) `WLS_DEBUG_SET_FOD_TYPE` enum: implicit enumerator values and NO trailing comma give the stock CRCs of `get_debug_fod_type` (0x77c9db9b) and `register_ops` (0x318daf76).
Providers are the generated OS4 stubs (`//re/stubs_a17B/os4`, exact stock export CRCs).

Result vs OS4 stock (compare_ko): 88 functions byte-identical, relocations, objects, 81 exports + CRCs, 16 imports + CRCs, modinfo -> EQUIVALENT.
Note for the other mca ports: the previous local `stubs_mcaB` headers are newer than the server copy; `stubs_a17B` was rebuilt from the local ones.
