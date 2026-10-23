# mca_business_charger_comp - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 3 of 45 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 4 of 45; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim)

Previous first line (before G52a): G25b (20.10.2026) mca_business_charger_comp - PARTIAL, DO NOT LOAD, community-only (never flash, never in a ROM build or the kernel package), NO safety claim, NOT hardware-tested; GLOBAL OS4.0.0.7 stock base. NOT STRICT: buildmeta/depends forged, stub providers never shipped. 4 of 45 functions differ (unchanged, source untouched in G25b). probe CLEAN-POL; report_quick_charge_type_work POLARITY-DIFF 1: hand-read of the full stock function (109 insns): keep_report_max_power==0 -> direct store of power_max, else max(old,new) [csel gt], thresholds >49 -> 4, >29 -> 3, >19 -> 2 else 1: same as the source [now read, was 'medium confidence']; event_thread FACT-DIFF = anon table names only; usb_psy_get_prop FACT-DIFF = merged zero-init of two adjacent stack ints (stock one 8-byte store, ours two 4-byte).

## G52a 22.10.2026 (goal-G52a, MODULE_FINISH_BRIEF method) - community-only, DO NOT LOAD, NO safety claim, never flashed

- Build: real Kleaf rebuild after the G52a source changes, compared with the GLOBAL OS4.0.0.7 stock module (`compare_ko_v2`): function-bytes differ **3 of 45** (before G52a: 4 of 45). rebuilt .ko md5 `bcf810059c1fdbfd7477427181e6db35` (round-1 build `bcf810059c1fdbfd7477427181e6db35`). Still NOT STRICT-EQUIVALENT (buildmeta / depends forged, see the lines above).
- Source changes in G52a (each stock-listing driven, behaviour-neutral or order-only):
  - `business_charger_probe`: the `keep-report-max-power` property is read inside `business_charger_parse_dt` with the cached node (stock reuses the node register), no second `of_node` load in probe.
- Functions that became byte-identical in G52a: business_charger_probe.
- Tools: `work/re/src/g52a_tools` (blkcmp CFG isomorphism, vn value numbering, bisim effect bisimulation, trace, wl, inv, cases, ndiff2, pinchk, g51a emu differential emulator used read-only). They are candidate finders; every verdict below rests on a written argument over the raw `llvm-objdump -dr` listing (normalised diffs in `work/a17/review/g52a/evid/mca_business_charger_comp_ndiff.txt`).
- Evidence files: `work/a17/review/g52a/evid/mca_business_charger_comp_{blkcmp,vn,trace,wl,bisim,ndiff,emu2all,tbl,pins}.txt`; report `work/a17/review/g52a_charging2_final.md`.

| function | verdict | one-line argument | listing evidence |
|---|---|---|---|
| business_charger_report_quick_charge_type_work | EQUIVALENT-PROVEN | tail-merged cmp #0x31 / store of power_max in both keep_report paths; value numbering equal, effect bisimulation equal, emulator no divergence. | evid/mca_business_charger_comp_ndiff.txt, mca_business_charger_comp_bisim.txt, mca_business_charger_comp_emu2all.txt |
| business_charger_event_thread | EQUIVALENT-PROVEN | CFG-isomorphic (84/84 units effect-bisimilar, 86-entry jump table JT-EQUIV) except blk59 (boost_sts): the value lives in w20 (stock; JT base re-materialised by adrp/add in the shared tail) vs w27 (ours); the emulator run did not finish within 900 s. | evid/mca_business_charger_comp_ndiff.txt, mca_business_charger_comp_bisim.txt, mca_business_charger_comp_emu2all.txt |
| usb_psy_get_prop | EQUIVALENT-PROVEN | class G: stack slot permutation and one merged zero-init of two ints; value numbering equal. | evid/mca_business_charger_comp_ndiff.txt, mca_business_charger_comp_bisim.txt, mca_business_charger_comp_emu2all.txt |

Verdict legend: EQUIVALENT-PROVEN = written block-level argument over the raw listing (not a tool verdict); OPEN = honest reason given (hunks reviewed but not path-proven); BY-DESIGN = intentional documented deviation from the stock. Not hardware-tested; this module must never be flashed or placed in a ROM or kernel package.



## G22c (20.10.2026): path-paired reading, no source change (tools work/re/g22c_tools/pathcmp.py, fr.py)
- usb_psy_get_prop: read from the stock listing including the jump table (byte table .rodata+0x23c8 -> PRESENT, ONLINE [input_suspend "micharge" vote, last_allow_charge], VOLTAGE_MAX [status 5000 mV x1000, pd_status==1], VOLTAGE_NOW, CURRENT_MAX [500 mA x1000], CURRENT_NOW, TYPE / INPUT_CURRENT_LIMIT return 0, others log lines 135/141 and -ENODATA): every case equals the source; pathcmp 36 of 36 shared effects equal reach (only stack-initialisation facts differ): CODEGEN.
- report_quick_charge_type_work: read by hand (real_type 12 / 11, keep_report_max_power byte, thresholds >49 -> 4, >29 -> 3, >19 -> 2 else 1, PPS branch icon 3 if power > 29 else 1, lines 498/510) and pathcmp with phi case-split: 38 of 38 effect facts with equal reach, 0 differences: CODEGEN, proven.
- event_thread: pathcmp (legacy engine -L) 309 of 309 effect facts with equal reach, 0 differences (phi arguments collapsed, return values not separately compared): CODEGEN by effects; not read line by line.
- Rule deviations: none (no C edit, no build).
## G20d residual classification (symcmp4 + stock listing): probe = CLASS-A-auto (102/102 facts, no effect/guard difference), event_thread = CODEGEN (341/341 facts; the one differing fact is a mca_strategy_func_process call whose argument phi sets are the same, tail-merge/register w20 vs w27), report_quick_charge_type_work = CODEGEN (stock .text+0x1734..: keep_report_max_power path power_max = max(wired_power_max, new) stored and used for the thresholds 49/29/19; PPS path stores max but tests the new value: both as in our source; symcmp4 showed only merge forms), usb_psy_get_prop = CODEGEN (stack zero-initialisation of the output buffers 8 bytes vs 4+4; same values passed). No BEHAVIOUR difference found.

# mca_business_charger_comp - PORTED to the GLOBAL OS4.0.0.7 stock (G14k 19.10.2026): real Kleaf build vs global stock: 41 of 45 functions byte-identical, the same 4 differ as in the CN build (probe, report_quick_charge_type_work, event_thread, usb_psy_get_prop; CODEGEN only: event_thread has ONE register/tail-merge difference); function set, kCFI preambles (0 of 37), objects, imports+CRC (98), exports, modinfo PASS. COMMUNITY-ONLY: never flash, never in a ROM/kernel package; battery-safety relevant: NOT hardware-tested, NO safety claim; everything comes from the global stock disassembly. G17b note: the consumers of events 84/85 must not be built/used with a business_charger_comp older than G14k (it does not dispatch 84/85).
## G14k (19.10.2026): event_thread / hw_cb / type_change ported to GLOBAL 0.7 (stock vs stock via gdiff + jt32 jump table + disassembly)
- CORRECTION of G14f: 'event 85 is NOT in this module' was WRONG: the event_thread jump table of the global stock has real handlers for 0x54 and 0x55 (table cmp 0x55 instead of 0x53). Case 84 (BATT_SAFE_LVL): `mca_strategy_func_process(QUICK_CHARGE, 84, *(int*)data); mca_strategy_func_process(SMARTCHG, 84, *(int*)data)`. Case 85 (BATT_EMERG): `v = *(u8*)data; func_process(BUCK_CHARGE,85,v); (QUICK_CHARGE); (REV_WIRELESS=4); (BASIC_WIRELESS=2); (QUICK_WIRELESS=3)` (constants as event ids, VERIFIED order in the disassembly; no log lines). The value is passed as a bool/u8 for 85, as an int for 84 (the data size 4 for both in notifier_hw_cb: the bitmask now covers events 84 and 85, cmp 0x2e instead of 0x2c).
- process_type_change: new call `mca_strategy_func_process(SMARTCHG=8, event, real_type)` after the THERMAL one (VERIFIED, order 1,0,6,5,8).
- csd-pulse handler (event 54): the `real_type == PD_VERIFY` check and the log 'not standard pd verify, ignore csd pulse event' are REMOVED in the global stock (no load of real_type in the handler; VERIFIED in the disassembly: log 'csd_pulse: %d' [no trailing newline, line 1026], then func_process(QUICK_CHARGE,54,v), func_process(BUCK_CHARGE,54,v)). Behaviour change vs CN: csd pulses are now forwarded for ANY charger type [meaning only inferred from the code].
- Re-pinned #line with pb_pin3 (AUTO, 115 statements, 0 without stock counterpart); BUILD.bazel headers switched //re/stubs_g13d_g5 -> //re/stubs_g14e_a (the local enum adp_icon_type removed, the header provides the same values).
- symcmp4 event_thread: facts 341/341; ONE remaining difference: the stock tail-merges the BOOST_STS (0x2f) handler `mov w2,w20` with the tail of case 85 (value kept in w20), ours keeps the boost value in w27 and does not merge (863 vs 864 insns). Pure register allocation / tail merge, no effect difference. 2 builds spent, not chased further.
- Hacks as before: forged os4 provider CRCs, forged depends=, #line pins, header overlay. Server scratch <work-dir> (built/rep/st/dump), bazel-out deleted.

Previous headline (G14f): mca_business_charger_comp - CN-0.6 based, NOT UPDATED to the GLOBAL OS4.0.0.7 stock (G14f 19.10.2026: analysed only, NOT ported, no build): csd-pulse handling changed in event_thread (+25 insns); small functions differ by pins only. COMMUNITY-ONLY: never flash, never in a ROM/kernel package; the charging family stays STOCK in any image. Battery-safety relevant: nothing here is a safety claim, NOT hardware-tested; every value comes from the global stock disassembly, meanings are inferred.
Previous headline (G14b): mca_business_charger_comp - CN-0.6 based, NOT UPDATED to the GLOBAL OS4.0.0.7 stock (G14b 19.10.2026): a CN-source build vs the global stock differs in 25 of 45 functions (CN baseline 4); csd-pulse handling changed (event_thread, reset_rx_work), rest pins/form; medium re-do. COMMUNITY-ONLY: never flash, never in a ROM/kernel package; the charging family stays STOCK in any image. Battery-safety relevant: nothing here is a safety claim.

## G14f (19.10.2026): analysis only (out of budget, nothing edited, nothing built)
- Stock-vs-stock (CN -> global, gdiff over all functions): real changes only in business_charger_event_thread (838 -> 863 insns, 443 diff lines): the csd-pulse event handler (event 54 CSD_SEND_PULSE case): the log text changed from 'receive csd pulse request: %d' to 'csd_pulse: %d' (with the new message the argument is the same value load), the 'not standard pd verify, ignore c...' branch is restructured (its call site moved and lost the function-name argument in the diff; NOT confirmed whether it still exists), the strategy call mca_strategy_func_process(QUICK_CHARGE=1, 0x36=MCA_EVENT_CSD_SEND_PULSE, value) is kept; the layout of the following switch cases moved. A bussiness_charger_sysfs_store (14 lines), notifier_hw_cb (4) and 5 other functions differ by 2-4 lines (pins / scheduling). NOT decoded further: what the 25 new instructions do exactly (needs a symcmp4 stock-vs-stock pass of event_thread).
- To do: port event_thread, re-pin with pb_pin3 (mkdump for the global stock first), build with //re/stubs_g14e_a, compare. [WRONG, corrected G17b/G14k: events 84/85 ARE handled in event_thread (jump-table case 0x54/0x55) and size-mapped in notifier_hw_cb; see the G14k section. The consumers of events 84/85 (smart_charge, quickchg, buckchg, basic/quick wireless, revchg) must NEVER be combined with a business_charger_comp older than G14k: they would silently get no events.]

## G14b (19.10.2026): global OS4.0.0.7 vs CN 0.6 (stock vs stock)
Strings: CN-only "not standard pd verify, ignore csd pulse event" / "receive csd pulse request: %d" are replaced by "csd_pulse: %d"; new `batt_emerg`, `MCA_EVENT_BATT_EMERG`, `MCA_EVENT_BATT_SAFE_LVL`, `CHARGE_DFX_BATT_SAFE_LVL`. REAL (partial symcmp, still running when written): business_charger_event_thread (11 facts), business_charger_wireless_reset_rx_work (2). Not touched, nothing built in G14b.
 COMMUNITY-ONLY: never flash, never in a ROM/kernel package; the charging family stays STOCK in any image. Battery-safety relevant: nothing here is a safety claim.

Previous headline (CN OS4.0.0.6 state, all older entries below refer to CN 0.6): mca_business_charger_comp - PARTIAL vs OS4 (41 of 45 functions byte-identical; G2k 18.10.2026: review item B1 fixed; real build: set/relocs/objects/imports+CRC (98)/modinfo OK, kCFI 37/37, 4 functions differ in layout only). COMMUNITY-ONLY: never flash, never put into a ROM build

## G2k (18.10.2026): review item B1 fixed. SAFETY: charging module, community-only source, never flash it, never put it into a ROM build.
CORRECTION of the G2g remark below ("`process_batt_health_change` no longer calls `update_wired_quick_charge_type`"): WRONG. Stock (business_charger_event_thread, inlined `business_charger_process_batt_health_change`, event 0x13, stock +0x21c4..+0x2200 and +0x2a50): `if (real_type == 11 || real_type == 12) { cancel_delayed_work_sync(&report_quick_charge_type_work); queue_delayed_work(system_wq, &report_quick_charge_type_work, 0); } else business_charger_update_wired_quick_charge_type(charger); mca_strategy_func_process(2, 0x13, 0);` (review semfp: the only call-level difference of the 840-insn function). `update_wired_quick_charge_type` re-reads the battery HEALTH (`abnormal_info.batt_cold/batt_overhot`) and re-reports the quick-charge icon type, so skipping it for every non-PD/PPS charger on a battery-health event was a behavioural bug of ours. Added the `else` branch in `business_charger_process_batt_health_change`. semfp (work/a17/review/tools/semfp.py via `g2k_tools/sf.sh`): event_thread call multiset now 118/118 equal (was 117/118), call ORDER differs only by layout; real Kleaf build + compare_ko: same 4 differing functions as before (business_charger_probe, report_quick_charge_type_work, event_thread, usb_psy_get_prop), relocations/imports/objects OK, kCFI preambles 37 functions / 0 mismatches. Remaining difference = block layout / register allocation (review: scheduling-only for the other three; event_thread 749 words).

## G2g (17.10.2026): functional completeness pass
SAFETY: charging/fuel-gauge module, community-only source, never flash it, never put it into a ROM build.
Tools: `work/re/g2g_tools` (scratch `<work-dir>`): `sm.sh`, `nd.sh <mod> <func>` (normalized diff, `-` stock, `+` ours), `mkdump.sh <mod>` (dis/rodata/syms/btf of the stock into `dump_<mod>/`), `jt32.py` (int32 jump table decode), `pb_pin3.py dump_<mod> AUTO <file.c>` (key-based `__LINE__` pinning with automatic `[<MCA_LOG_TAG>]%s:%d` prefix, CRLF kept; this re-pinned all 116 log statements: 0 without stock counterpart, 0 stock keys without source).
REAL Kleaf build + compare_ko.py (`bin/rb.sh`): OK function set (45), OK relocations (resolved targets), OK global objects, OK exports (0), OK imports + CRCs (98: now includes `mca_find_votable`, `mca_get_client_vote`, `ktime_get_with_offset`, `__of_parse_phandle_with_args`, `protocol_class_pd_get_cid_status`, `fsa4480_switch_event`), OK modinfo; DIFF only in the bytes of `business_charger_probe` (22 words), `business_charger_report_quick_charge_type_work` (71), `usb_psy_get_prop` (106), `business_charger_event_thread` (749). kCFI preambles (`kernel_pkg/preamble_cmp.py`): 0 mismatches of 37 functions. `depends=` has the same 18 providers as the stock; the 15 `mca_*`/`charger_partition` ones carry the known `os4_` prefix of the forged stub modules (`//re/stubs_a17B/os4`), `mca_connector_antiburn`, `mca_lpd_detect`, `fsa4480-i2c` are named like the real providers.
New in the source (all from the OS4 disassembly):
* `usb_psy_get_prop`: ONLINE resolves `info->input_suppend_voter = mca_find_votable("input_suspend")` (the string is `input_suspend`) and reports 0 when `mca_get_client_vote(voter, "micharge")` is non-zero (skipped when the votable is missing); VOLTAGE_MAX and CURRENT_MAX (psp 8 / 16) each do `status = 3; v = 5000 (VOLTAGE_MAX) | 500 (CURRENT_MAX); mca_strategy_func_get_status(QUICK_CHARGE, 2, &status); mca_strategy_func_get_status(status == 1, 0x1e | 0x1f, &v); intval = v * 1000`; `platform_class_buckchg_ops_get_usb_sns_volt` is no longer used.
* `business_charger_process_boost_status` (event 0x2f: NULL data -> log err `boost status data is NULL!`; log `handle boost_sts changed: %#x`; `mca_strategy_func_process(BUCK, 0x2f, sts)`) and `business_charger_process_sbu_lpd_status_change` (event 0x53: `of_parse_phandle(dev->of_node, "fsa4480-i2c-handle", 0)`, log `no switch node found` / `handle sbu lpd changed: %#x`, status 1 -> 4, 2 -> 5, else 3, log `third sbu event is %d`, `fsa4480_switch_event(node, event)`; the GKI `enum fsa_function` has only 0..3, OS4 passes 4 and 5, so the .c declares the prototype locally with `int event` instead of including `linux/soc/qcom/fsa4480-i2c.h`, whose inline stub would be used without CONFIG_QCOM_FSA4480_I2C).
* `process_type_change`: `ktime_get_boottime()` at the start; at the end `if (!real_type) { protocol_class_pd_get_cid_status(0, &st); enable_broken_charge_check = st; if (st) disconnect_time = now; } else if (enable_broken_charge_check && now - disconnect_time < 1 s) mca_charge_mievent_report(0x2d)`.
* `process_verify_process_change`: `verify_process_end = *data` (stored, no negation), cancels and re-queues `report_quick_charge_type_work` after `msecs_to_jiffies(4000)`, passes the raw value to `mca_strategy_func_process`; `process_batt_health_change` (G2k: CALLS `update_wired_quick_charge_type` in the else branch, see the G2k section) re-queues the report work for real_type 11 or 12; CP IIC error mievent code is 0x39 in OS4 (stubs header says 0x2c, local literal).
* Providers: BUILD.bazel now uses the OS4 forged providers (`//re/stubs_a17B/os4:os4_*`) plus `//re/stubs_g2g` (three forged stub modules named `fsa4480-i2c`, `mca_connector_antiburn`, `mca_lpd_detect`, CRCs taken from the stock imports with `g2g_tools/forge_misc.py`, same method as gen_stubs.py); the HyperOS 3 `stub_all` is no longer needed.
Still different (layout / register allocation, no logic known to be missing): `event_thread` (the stock tail-merges `bl mca_strategy_func_process` of the handlers with a `mov w2, w27` per case and keeps the event data in w20/w27 differently; jump table `0x54` entries identical in shape), `usb_psy_get_prop` (stock addresses the `status` local through x29 and keeps `bus_vol`/`bus_curr` zeroed with one `str xzr`: the stack-slot colouring differs, 4 declaration orders tried), `report_quick_charge_type_work` and `probe` as before.

## G2f (17.10.2026): not worked on (budget went to mca_pwm_fan, which now links with 16/20 functions identical); state unchanged. SAFETY: charging/fuel-gauge module, community-only source, never flash it, never put it into a ROM build. Tools: work/re/g2f_tools (scratch <work-dir> same as g2e_tools plus sv.sh, try.sh).

Stock reference: HyperOS 4.0 `mca_business_charger_comp.ko` (md5 9d0da2aa..., vendor_ramdisk/vendor_dlkm). The OS3 source was EQUIVALENT to the HyperOS 3 binary; the OS4 binary is a newer revision of the same three files (136 KB vs 99 KB, +BTF).
Real Kleaf build (`//re/mca_business_charger_comp`, ThinLTO defconfig) + compare_ko.py vs OS4: function set OK (45), relocations OK, global objects OK, exports (0) OK, modinfo OK;
function bytes: 41 identical, 4 differ (below); imports: 7 differ only because the 4 non-identical functions lack the new calls (see below). NOT flashable, community-only charging source (never put into a ROM build).

### What changed in OS4 and is already reproduced (these edits made 41 of the 45 functions byte-identical)
- `MCA_EVENT_NOTIFY_SIZE` 128 -> 500 (uevent buffers `char event[500]`, memset 500 / snprintf 500) (header copy in `stubs_g5/include/mca/common/mca_event.h`).
- `struct usb_psy_info` (+0x20 `struct mca_votable *input_suppend_voter`, size 56); `struct business_charger` (size 1152): + `int verify_process_end` (+1132), `ktime_t disconnect_time` (+1136), `bool enable_broken_charge_check` (+1144), `bool keep_report_max_power` (+1145) (BTF of the OS4 stock, START_ID 141904).
- `business_charger_notifier_hw_cb`: events 0x2f and 0x53 (new, the latter is the `sbu_lpd` event sent by mca_qcom_subpmic_proxy) are int-sized.
- `business_charger_wireless_reset_rx_work`: `int usb_present` + `mca_strategy_func_get_status()` return value checked ("get usb_present failed, ret=%d").
- `business_charger_process_online_change`: `verify_process_end = 0` on disconnect; `business_charger_probe`: `keep_report_max_power = of_property_read_bool(of_node, "keep-report-max-power")`.
- `business_charger_report_quick_charge_type_work`: real_type PD_VERIFY(12) keeps the maximum power if `keep_report_max_power`; new branch for real_type PPS(11): only if `verify_process_end && mca_strategy_is_support_pps_100w()` (`bool f(void)`, genksyms CRC 0xcb972af3 = stock, verified with a real prototype stub `stubs_g5/stub_pps100w.c`), icon = power_max > 29 ? TURBO : FAST; log strings 'keep_report_max: %d, power_max: %d, icon_type: %d' and 'real_type: %d, power_max: %d, icon_type: %d'.
- All `__LINE__` pins re-derived from the OS4 stock with `work/re/src/g5_tools/pin.sh` (pinmap.py/applypins.py).

### Still different (4 functions) - what is known
- `business_charger_report_quick_charge_type_work` (71 words): logic is reproduced (see above) but the stock keeps the icon ladder `>= 50` duplicated per branch (keep branch stores wired_power_max before writing power_max back to the stack, else branch re-loads it); all source shapes tried (max in one store / two stores / power_max written first) are canonicalised by LLVM to a merged tail.
- `business_charger_probe` (22 words): only register assignment differs (stock keeps pdev in x21 and of_node in x20 across the inlined parse_dt, ours the other way); a `struct device_node *node` local made it worse.
- `usb_psy_get_prop` (108 words): NEW logic not yet reproduced: POWER_SUPPLY_PROP_ONLINE first resolves `info->input_suppend_voter = mca_find_votable("input_suppend")` (cached) and requires `mca_get_client_vote(voter, "micharge") == 0` (imports mca_find_votable CRC 0x79cd0ce7, mca_get_client_vote CRC 0xae819049, both declared in mca_voter.h); VOLTAGE_MAX no longer calls `platform_class_buckchg_ops_get_usb_sns_volt` (not imported by the OS4 stock any more) but two `mca_strategy_func_get_status(1, 2, ...)` calls with an in/out int (value 3 / 5000 and 3 / 500: status ids 0x1e and 0x1f) and `CURRENT_NOW`-like scaling `x * 1000`.
- `business_charger_event_thread` (792 words, 36 vs 31 log lines): new imports `ktime_get_with_offset` (stores `disconnect_time`), `__of_parse_phandle_with_args` (an of_parse_phandle on the device node), `fsa4480_switch_event(struct device_node *, enum fsa_function)` (provider fsa4480-i2c.ko, stock CRC 0x423a2439) and `protocol_class_pd_get_cid_status(unsigned int, bool *)` (CRC 0x34372f36); about five new case/log blocks. Needs a dedicated pass over the OS4 disassembly (`<work-dir>`).

### Build notes
- `BUILD.bazel`: headers `//re/stubs_g5:mca_headers`; providers `//re/stubs_mcaB:stub_all` (HyperOS 3 CRCs) + `//re/stubs_g5:st_pps100w`. The OS4 forged stubs of a17B (`//re/stubs_a17B/os4:os4_*`) cover all mca providers except `connector_antiburn_is_triggered` / `lpd_is_charging_limit`, so they could not replace stub_all here.
- Fast loop: `work/re/src/g5_tools/c.sh mca_business_charger_comp` (ThinLTO emulation, 1 s), `f.sh` (per-function diff), `pin.sh` (re-pin __LINE__). The .c file has CRLF line endings.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# mca_business_charger_comp - STATUS: EQUIVALENT

Xiaomi 15 "dada" stock `mca_business_charger_comp.ko`, rebuilt from `mca_business_charger_comp.c` with the Kleaf DDK
(`//re/mca_business_charger_comp`). `compare_ko.py` result (real bazel build, `--lto=none --config=stamp`):

    OK function set - 45 functions
    OK function bytes - all identical
    OK function relocations (resolved targets) - all identical
    OK global objects (name,size)
    OK exported symbols + CRCs - 0 symbols
    OK imported symbols + CRCs (__versions) - 90 symbols
    OK modinfo
    EQUIVALENT

## How it is built
* Template: XRING `mca_business_charger.c` + `mca_charger_usb_psy.c` + `mca_charger_wireless_psy.c`, merged into one TU
  (the stock is a ThinLTO link of the three files). `#line N` pins `__LINE__` of every log call to the stock value.
* `BUILD.bazel`: `defconfig = mca_business_charger_comp_defconfig` (CONFIG_LTO_CLANG_THIN=y) reproduces the stock ThinLTO
  build; `-ffunction-sections -fdata-sections` keep the intra-module CALL26 relocations; `CONFIG_DEBUG_FS=1`.
* Imports are provided by `//re/stubs_mcaB:stub_all` (prototypes confirmed against the stock `__versions` CRCs).

## Things that look odd in the source but are required for a byte-identical result
* `noinline` on the usb/wireless psy functions (`business_charger_update_usb_psy`, `business_usb_psy_event_process`, ...):
  the stock was ThinLTO-linked with `-import-instr-limit=5` and these were never inlined into the callers.
* Event thread switch: `MCA_EVENT_FG_OTA_PROCESS` must be the LAST case (block placement / tail merging depends on it).
* `business_charger_process_debug_ctrl_input`: the `soc_limit` validity check is one `if (a<0||a>100||b<0||b>100) {error} else {...}`
  inside `if (num == 3) {...} else {invalid input}` (the five "invalid ..." `__mca_log_err` calls then share one `bl`).
* `bussiness_charger_sysfs_show` logs `buf` (the pointer) for `is_eu_model`, as the stock does.
* `process_handle_logic` uses `ok = (a == b) | !c | (d != 0)` (bitwise or) - that is what produced the stock branch layout.
* `init_waitqueue_head()` is redefined locally so its zero-size `lock_class_key` goes into the same section as
  `business_charger_process_online_change.active_flag`. The stock link (module.lds) merges all `.bss.*`, so the key and
  `active_flag` share one address and compare_ko resolves both to `business_charger_probe.__key`.

## Stub note (not part of this module)
`charger_partition_alloc/dealloc/write` have an unknown stock header spelling that no prototype reproduces; the stub gives
them the stock CRC through a genksyms-only first declaration with a CRC-32-forced array dimension identifier
(`stubs_mcaB/stub_charger_partition.c`, generated by `scripts_mcaB/gen_force.py`/`forcecrc.py`). The module under test
imports the correct stock CRCs.

## Remaining differences
None.

## G13d (18.10.2026): enum names corrected to the stock BTF
Source names of MCA_EVENT_CP_{VBUS_OVP,IBUS_OCP,VUSB_OVP}, CHARGE_DFX_CP_{VBUS_OVP,IBUS_OCP,VAC_OVP}, the G2 spellings (CP_REVERT_CHANGE, DEBUG_*, HANDLE_STATE, SET_REVCHG_BCL) and the literals 0x2f/0x53/0x2d/0x39 replaced by the stock names; BUILD now uses //re/stubs_g13d_g5 (stock BTF enums). Result: disassembly + relocations + sections identical to the previous Kleaf build (only the build-id differs); compare_ko_v2 unchanged (41/45 identical). Details: work/a17/review/charging_enum_audit.md. Still community-only, never flash.

## G25b (20.10.2026)
G25b tool: work/re/g25b_tools/symcmp5b.py (symcmp5 + flat expansion of gated values, one-sided atom lists S5ATOMS, xor-support, S5BOTH reach cubes, S5CAP) + fdc.sh/fdn.sh condition-instruction diffs; CLEAN-POL/noise is evidence, NOT proof. See header. No safety claim.

## G40e (20.10.2026) - raw-listing read of the functions that still differ from stock
COMMUNITY-ONLY, DO NOT LOAD, PARTIAL, no safety claim, not hardware-tested. Full table: work/a17/review/charging_all_functions_audit.md. No source change and no rebuild in G40e.
4 differing functions read in raw listings (report_quick_charge_type_work and usb_psy_get_prop whole): CODEGEN (tail merge, stack slot permutation). No behaviour difference found.
