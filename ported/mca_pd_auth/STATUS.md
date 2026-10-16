# mca_pd_auth - EQUIVALENT to the HyperOS 4.0 (OS4) stock module

SAFETY: charging module (`mca_*`). The source is for the community only; it must never be flashed or put into a ROM build.

Reference binary: `<work-dir>` (identical copy in vendor_dlkm), vermagic 6.6.57-android15-8.
Template: XRING tree `work/re/xiaomi-modules/mca/mca_strategy/strategy_pd/mca_pd_auth.c` + `pd_auth.h` (public `mca/...` headers reconstructed in `work/re/src/stubs_a17A/include`).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, `compare_ko.py`): EQUIVALENT = 8 functions byte-identical (incl. `__LINE__`), relocations, global objects, 0 exports, 33 imports with CRCs, modinfo.

Changes relative to the template (the OS4 stock is a newer source than the XRING copy):
- `struct pd_auth_strategy` has only `dev` and `pd_verified_type` (stock offset +8); `verify_porcess_end` is gone from the struct and probe. The verify_process case sets a stack local `verify_process_end = !value` after `protocol_class_pd_set_verify_process()` and passes its address to `mca_event_block_notify()`.
- `strategy_pd_auth_get_vdm_cmd()` keeps the return value of `protocol_class_pd_get_vdm_cmd()`; in the AUTHENTICATION case `if (rc) { mca_log_err("get auth data failed, rc = %d\n", rc); return snprintf(buf, PAGE_SIZE, "%d,Null\n", cmd + USBPD_UVDM_CMD_INIT); }` (new string in the stock rodata).
- Removed `#include <linux/power_supply.h>` and `<mca/common/mca_charge_interface.h>` and one blank line, which makes the `__LINE__` immediates of every log call equal to the stock (net -5 lines vs the template + new code). Needed system includes were moved into `pd_auth.h`.
- Headers: `stubs_a17A/include/mca/protocol/protocol_pd_class.h` now declares the 20 exports used by the a17A group (PROTOCOL_PD_MAX_STRING_LEN=64, PDO_NUMS=7, struct usbpd_vdm_data / pd_pdo, enum uvdm_state, XM_REQUEST_PD_DR_UFP/DFP=1/2); `stub_mca_protocol_pd_class.c` is generated from it (`gen_stub.py`) and all import CRCs equal the stock.

Notes: OS3 and OS4 stock differ in 3 of 8 functions (probe, show, store) - the OS3 tree of this module is NOT the same source.
Build: `bash work/re/src/a17_push.sh mca_pd_auth` then on the server `<work-dir> mca_pd_auth` (own stock dir `<work-dir>`, OS4 binaries).
