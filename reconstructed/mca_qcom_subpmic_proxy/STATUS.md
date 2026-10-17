# mca_qcom_subpmic_proxy - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `mca_qcom_subpmic_proxy.ko` (md5 0b26a0a8..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/mca_qcom_subpmic_proxy`) + compare_ko.py (EQUIVALENT).

CHANGED in OS4 and fixed: (1) `struct platform_class_buckchg_ops` grew by one op `int (*set_cp_vendor_id)(void *, int *)` (+656, size 664; kCFI 0x72d0a315 = int(void*,int*)); the import CRC of platform_class_buckchg_ops_register with the reconstructed struct equals the stock OS4 CRC 0x129fc036 (real prototype stub `stubs_g5/stub_buckchg_register.c`, no forging); (2) new function `qcom_subpmic_set_cp_vendor_id()` (log 'set_cp_vendor_id = %d', glink write prop 0x202c, 8 bytes); (3) `struct qcom_subpmic` +8 bytes (new `int sbu_lpd` at +0x314, cid_status at +0x318, kzalloc 0x320); (4) notify ids: new id 11 = sbu_lpd (event 4/0x53), cid_sts is now 12; (5) `qcom_subpmic_notify_change_work` now kfree()s every node (OS3 leaked it); (6) ship mode skips the ovpgate disable also for cp vendor 4 (`chip_vendor != 0 && chip_vendor != 4`); (7) all __LINE__ pins re-derived from the OS4 stock by g5_tools/pin.sh. compare_ko vs OS4: function set (82), bytes, relocations, objects, exports+CRC (3), imports+CRC (51), modinfo all OK. Headers: `work/re/src/stubs_g5/include` (copy of stubs_mcaA platform_buckchg_class.h + the new op).

SAFETY: charging module, community-only source: never flash it and never put it into a ROM build.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# mca_qcom_subpmic_proxy - EQUIVALENT

`compare_ko.py`: function set (81), function bytes, resolved relocations, global objects, exports + CRC (3: `register_hboost_event_notifier`,
`unregister_hboost_event_notifier`, `qti_battery_charger_get_prop`), imports + CRC (49), modinfo: all OK.
All 9 data objects (buckchg ops table, bc12 ops, qc ops, glink callbacks, match table, platform driver incl. `.owner`, hboost notifier head, ...): identical.
Only vermagic/intree/scmversion/built_with differ (build environment).

No XRING counterpart exists (Qualcomm only): the file is written from scratch from the disassembly. It proxies the MCA buck charger / BC1.2 / QC protocol classes to the ADSP
charger firmware through `mca_adsp_glink_{read,write}_prop` (property ids = macros `QCOM_PROP_*`), and handles the ADSP notifications (usb change, plugin, enable boost, LPD,
PD state, CC short vbus, PPS PTF, bsink suspend, CP revert, OTG CP config, CID status) through `qcom_subpmic_notify_cb` + a deferred list/work (`notify_change_work`).

Things that were not obvious and are needed for byte identity:
- `#define CONFIG_QTI_BATTERY_CHARGER 1` before `<linux/soc/qcom/battery_charger.h>` (otherwise the header only has `static inline` stubs and the three exports clash).
- Build: ThinLTO `defconfig` (`mca_qcom_subpmic_proxy_defconfig`), `-ffunction-sections -fdata-sections`, `CONFIG_LTO=1`; `#line` pins for every log statement
  (for the 2-line `mca_log_err` of the `dts sync otg_cfg_src` message the pin is the line before it, `__LINE__` = closing paren).
- Statement order matters for the optimizer: `qcom_subpmic_probe` initializes `notify_list` / `notify_lock` BEFORE the three `INIT_*_WORK`s, and uses `device_init_wakeup()`;
  `set_qc_volt` declares `set_volt` before `vbus` (stack slot order); `update_usb_type_work` declares `real_type, pd_active, usb_online` and logs `usb online` with `mca_log_err`;
  `get_otg_boost_src` logs `*otg_boost_src` (store-to-load forwarded) while returning the reloaded `chip->otg_boost_src`;
  in `notify_cb` the lpd/cc_short/pps_ptf logs print `chip->lpd_status` / `chip->cc_short_vbus` / `chip->pps_ptf` (not `*val`).
- `notify_change_work`: the switch cases must be written in the order PLUGIN(2), ENABLE_BOOST(3), PD_STATE(5) (LLVM emits the compare chain in reverse source order); the
  `val = node->val` copy is taken AFTER `mca_event_block_notify(4, 0x2f, &node->val)`; the local `od = &chip->otg_data` lives inside the ENABLE_BOOST arm.
- `chip->otg_data` is the CP_REVERT payload `auth_pos` (consumer: `strategy_buckchg_cp_revert_handler`: `auth = auth_pos >> 16`, `pos = auth_pos & 0xffff`). It must be declared with
  16-bit BITFIELDS (`union { int val; struct { unsigned pos:16; unsigned auth:16; }; }`) and set with `od->auth = 0x100`: the load/and/or/store RMW is narrowed by the DAG combiner into
  `strh w8, [x21, #2]` based on the shared pointer register. Plain `u8 b[4]` / `u16` stores give `[x19, #0x27e]` (2 words different, found after ~25 variants).
- ops tables: `g_qcom_buckchg_ops` (82-op struct from BTF), `g_qcom_pmic_bc12_ops` (`bc12_det_en`, `get_charge_type`), `g_adsp_qc_ops` (non-static, exported symbol in stock `.data`).
Unused/unknown memory: `reserved1[0x110]` and `reserved2[0x34]` in `struct qcom_subpmic` are never touched by the stock code (kept as padding to get the 0x318 size / member offsets).
Providers: stub modules in `work/re/src/stubs_mcaA` (mca_log, mca_event, mca_hwid, mca_parse_dts, mca_adsp_glink, bc12 / qc / pd / cp / buckchg / strategy classes); all provider CRCs match the stock exporters.
