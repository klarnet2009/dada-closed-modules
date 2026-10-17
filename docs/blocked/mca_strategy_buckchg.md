# mca_strategy_buckchg - BLOCKED before compile (scaffold only; reference = OS4 stock, 3 functions more than HyperOS 3)

SAFETY: charging module (`mca_*`): source for the community only, never flash it / never put it into a ROM build.

Template: XRING `mca/mca_strategy/strategy_charger/mca_strategy_buckchg.c` (2406 lines) + `inc/mca_strategy_buckchg.h` (flattened); BUILD/defconfig generated, 19 OS4 stub providers.

OS3 -> OS4 stock: OS4 adds `strategy_buckchg_exit_wireless_revchg`, `strategy_buckchg_is_support_pps_100w`, `strategy_buckchg_otg_monitor_workfunc`; 20+ functions differ; imports changed (new `mca_strategy_is_support_pps_100w`,
`mca_wireless_rev_get_user_reverse_chg`, changed `mca_smartchg_if_ops_register` CRC; the OS3 `platform_class_cp_get_pmid2out_uvp_dis` is gone).

First blocker: `#include <mca/common/mca_smem.h>` (absent public header of the smem provider: prototypes of the `mca_qcom_smem` exports, see `work/re/src/mca_qcom_smem` and `stubs_a17B/include/mca/qcom/mca_qcom_smem.h`),
then the same classes of missing enumerators as in mca_quick_wireless/STATUS.md (not enumerated yet, the fatal include stops clang).

G2: `stubs_a17B` now has the OS4 `struct mca_smartchg_if_ops` (88 bytes, `is_support_pps_100w` callback = `bool f(void)`; the stock `strategy_buckchg_is_support_pps_100w` (kCFI 0x36b1c5a6, reads `g_buckchg_info->support_pps_100w` at +0x830 / mca_get_hwid_info()) is that callback) and 6 `enum mca_smartchg_if_chg_type` values, see mca_charger_thermal/STATUS.md for the list of header changes. The `mca/common/mca_smem.h` blocker is unchanged.
