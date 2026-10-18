#!/usr/bin/env python3
# G13o: prepend a truthful first line and append a G13o section to the STATUS.md of the touched modules (keeps the line ending style of the file)
import os, sys
ROOT = r"<workdir>/work\re\src"


def upd(mod, first, section):
    p = os.path.join(ROOT, mod, "STATUS.md")
    s = open(p, encoding="utf-8", newline="").read()
    nl = "\r\n" if "\r\n" in s else "\n"
    i = s.index(nl)
    old_first = s[:i]
    new = first + nl + nl + "(status line before G13o, kept for history:) " + old_first.replace("# ", "", 1) + s[i:]
    new = new.rstrip() + nl + nl + section.replace("\n", nl) + nl
    open(p, "w", encoding="utf-8", newline="").write(new)


upd("mca_strategy_buckchg",
    "# mca_strategy_buckchg - PARTIAL (G13o 19.10.2026): real Kleaf, 48 of 61 functions byte-identical (13 differ, was 16), kCFI 45/45, preamble_cmp 0 diffs; the hidden-behaviour audit (symcmp4 on EVERY differing function) found and fixed 3 real differences (master_batt_missing override release, two pmic_temp_term clears); the other 13 differ only in form (see work/a17/review/charging_hidden_audit.md). NOT STRICT (buildmeta, depends, bytes of 13 functions, 1 anon-data chunk). COMMUNITY-ONLY: never flash, never in a ROM build; NOT hardware-tested, NO safety claim",
    "## G13o (19.10.2026) hidden-behaviour audit\n"
    "- Fixed (stock evidence; byte-identical afterwards): process_batt_btb_change released the `master_batt_missing` override at 'battery missing' too (stock only at value == 0, stock .text+0x5d2c); if_set_chg_en cleared the `pmic_temp_term` overrides on a disable request (stock only on enable, +0x6db8); if_set_input_suspend the same (stock only on suspend release, +0x6c2c). The earlier ports had missed the conditionality.\n"
    "- Checked, class (a) (effects and guards equal): class_probe, monitor_workfunc (guards differ only by the commuted pair soc<=94 / info+1512==0), base_flip_sw_cv_workfunc, wls_revchg_monitor_workfunc, source_status_monitor_workfunc, process_event, get_status, set_vterm, set_charge_volt, process_antiburn_change (log __LINE__ pins 2027/2035 of the inlined set_charge_volt copy are swapped vs stock: cosmetic, left), process_csd_pulse, cp_revert_handler, is_pdo_9v.\n"
    "- Full list and method: work/a17/review/charging_hidden_audit.md; tool work/a17/review/tools4/symcmp4.py; server <work-dir>")

upd("mca_basic_wireless",
    "# mca_basic_wireless - PARTIAL (G13o 19.10.2026): real Kleaf, 63 of 73 functions byte-identical (unchanged), kCFI 58/58, preamble_cmp 0 diffs; hidden-behaviour audit: 9 of the 10 differing functions are form-only (class a), process_irq stays NOT equal (mievent type/value mapping unproven, TX range gate, 38 only-ours stack facts); tx_q1/tx_q2 buffers now kzalloc as in the stock. NOT STRICT (buildmeta, depends, bytes of 10 functions). COMMUNITY-ONLY: never flash, never in a ROM build; NOT hardware-tested, NO safety claim",
    "## G13o (19.10.2026) hidden-behaviour audit\n"
    "- Fixed: strategy_wireless_parse_q_vaule used kmalloc/kmalloc_array for tx_q1/tx_q2; the stock probe zeroes them (__kmalloc flags 0xdc0): now kzalloc (no visible effect: read only after a successful parse). class_probe facts 456/456.\n"
    "- Class (a): get_adapter_charger_mode, split_irq_index, base_flip_sw_cv_workfunc, adapter_handle, sysfs_store (equal); monitor_work (loop counter 15 down / -15 up); max_power_control_work (stock logs from two sites with lines 3582/3587, merged to one call: log line only); process_event (phi noise only); class_probe.\n"
    "- NOT equal: process_irq (stock .text+0x5f00): one mievent site with computed (type 26..29, value 0..2) in stock, two sites in ours; TX voltage range 20000|32000 gate (platform_version 3, dada is 2); log line 2534.\n"
    "- Details: work/a17/review/charging_hidden_audit.md.")

upd("mca_quick_wireless",
    "# mca_quick_wireless - PARTIAL (G13o 19.10.2026): real Kleaf, 29 of 37 functions byte-identical (was 28; can_tbat_do_charge and get_parallel_volt_para are now identical: they had REAL differences, the earlier claim 'differ only in the real build' was wrong), kCFI 18/18, preamble_cmp 0 diffs; fixed: proc_data.zone_changed (set in can_tbat_do_charge, consumed in select_stage), parallel stage `!=`, no extra get_parallel_volt_para in select_stage, log level of 'no volt para'. Still open (not equal): process_event (FG_OTA_PROCESS, CP_NEW_MODE, events 69..73), parse_batt_info (93 stock-only facts), select_max_ibat (base flip only). NOT STRICT (buildmeta, depends, snprintf import, objects, anon-data). COMMUNITY-ONLY: never flash, never in a ROM build; NOT hardware-tested, NO safety claim",
    "## G13o (19.10.2026) hidden-behaviour audit\n"
    "- Fixed (stock evidence): `zone_changed` (info+920, the BTF name existed but was never used): stock can_tbat_do_charge +0x3b24 sets it when the temperature zone changes, select_stage +0x3554..0x356c re-selects the stage when `stage > cur || zone_changed || ffc differs` and clears it; ours had no re-selection (the stage stayed on the old, lower-current value). The parallel (base flip) stage update is `stage != parall_cur_stage` in stock and select_stage does not call get_parallel_volt_para. 'no volt para' in get_parallel_volt_para is __mca_log_err in stock.\n"
    "- Class (a): monitor_work, stop_charging (400 mA clamp as `< 401` vs folded `< 400`: same result), change_bridge (loop counter direction), select_cur_work_mode, select_stage (phi labels).\n"
    "- Open, class (b), known: process_event (stock .text+0x1670; events 2..73 vs ours 2..68: fg_ota_process vote at +0x430, CP_NEW_MODE, soc stop +0x3f8, taper_done_no_retry reset +0xa8), parse_batt_info (+0x1dc8), select_max_ibat (+0x3d7c; base flip only, inactive on dada).\n"
    "- Details: work/a17/review/charging_hidden_audit.md.")
print("ok")
