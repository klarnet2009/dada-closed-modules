# mca_qcom_panel - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `mca_qcom_panel.ko` (md5 55a2ee7b..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/mca_qcom_panel`) + compare_ko.py (EQUIVALENT).

unchanged since HyperOS 3: the source needed no change; real Kleaf build + compare_ko.py vs the OS4 stock: function set (9), bytes, resolved relocations, global objects, exports+CRC (2), imports+CRC (21), modinfo all OK.

SAFETY: charging module, community-only source: never flash it and never put it into a ROM build.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# mca_qcom_panel - EQUIVALENT

`compare_ko.py`: function set (9), bytes, resolved relocations, global objects, exports+CRC (2), imports+CRC (21), modinfo: all OK.
Extra data check (`.data/.bss/.rodata` objects: `mca_panel_driver`, `match_table`, `retry_count`, `probe_cnt`, bytes + relocations): identical.
Only modinfo `vermagic`/`intree`/`scmversion`/`built_with` differ (build environment, unavoidable); `depends` identical.

Source: `mca_qcom_panel.c` - Qualcomm `panel_event_notifier` variant of the XRING `mca_xring_panel.c` (same vendor code base).
Differences vs the XRING file that were recovered from the stock binary:
- notifier registered with `PANEL_EVENT_NOTIFICATION_PRIMARY` / `PANEL_EVENT_NOTIFIER_CLIENT_BATTERY_CHARGER`, handler gets `struct panel_event_notification` (`notif_type`);
- `of_parse_phandle()` without `of_node_put()`, `-EPROBE_DEFER` is silent, "Failed to find active panel, rc=%d" is new;
- the cookie is stored (`panel_dev->notifier_cookie`) only on success; "register panel notifier successful" is `mca_log_err` (not info);
- `g_panel` is never assigned, so `mca_panel_get_screen_state/hbm_state` compile to `return 0`.
- `__LINE__` of every log call is pinned by padding comments/blank lines (lines 67,71,86,101,125,137,141,155,166,170,181).

Provider interfaces: `//re/stubs_mcaA:{mca_log_stub,mca_event_stub,panel_event_notifier_stub}`; headers `stubs_mcaA/include/{mca/common,soc/dada}`.
The `panel_event_notifier_*` enums must contain the extra `PANEL_EVENT_NOTIFIER_CLIENT_{THERMAL,THERMAL_SECOND,KEYBOARD}` (taken from the BTF of the stock
panel_event_notifier.ko), otherwise the CRC of `panel_event_notifier_register` differs.
