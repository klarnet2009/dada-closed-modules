# batt_safe / batt_emerg chain review (GLOBAL OS4.0.0.7, read-only)

Reviewer: independent adversarial review (-batt-emerg), 19.10.2026 ~20:45 local.
Scope: behaviour equivalence of OUR reconstructed sources vs the GLOBAL OS4.0.0.7 stock for the "battery safe level / battery emergency" feature.
NOT a safety statement. Nothing was built, loaded, flashed or run on a phone. No git. Sources not modified.

Method: own `llvm-objdump -dr` (clang-r510928) of the stock modules in `<work-dir>` (identical to vendor_ramdisk copies),
`llvm-readelf -p .rodata` for strings, jump tables decoded from the raw `.rodata` bytes; scratch `<work-dir>` (dumps only).
Our side: the sources below (md5 prefix at review time) plus, where useful, the already built `.ko` of G14e/G14f/G14k.
Note: goal-G14k was editing business_charger_comp / quickchg / quick_wireless while this review ran; the state reviewed is the md5 below.

```
8acb873d1656 charger_partition/charger_partition.c      7bfcbee83e71 mca_smart_charge/mca_smart_charge.c
6aa7ef5ab168 mca_strategy_fg_comp/mca_strategy_fg.c       fe595e51d9a2 mca_smart_charge/mca_smart_charge.h
8607e17aae55 mca_strategy_fg_comp/strategy_fg.h          f4fe06752895 mca_strategy_buckchg/mca_strategy_buckchg.c
c006d57c60b6 mca_strategy_quickchg/mca_strategy_quickchg.c 72760c789ead mca_strategy_quickchg/mca_quick_charger.h
973d2cf056f5 mca_quick_wireless/mca_quick_wireless.c     70e4c1558dde mca_basic_wireless/mca_basic_wireless.c
5e1c0de50827 mca_wireless_revchg/mca_wireless_revchg.c   0cb0d856d24c mca_business_charger_comp/mca_business_charger_comp.c
```

## Verdict

The chain in our sources is behaviour-equivalent to the stock in every place that has an effect, with ONE low-severity code difference
(smart_charge workfunc, DFX-only) and one return-value difference nobody consumes. The most important findings are about WHAT THE STOCK
ITSELF DOES: two of the requested limits are no-ops in the stock (SoC 60 % and the quickchg "batt_safe" current ratio), and our sources
reproduce that correctly, but our documentation (G14f entry, quickchg/smart_charge comments and STATUS) describes the quickchg "batt_safe"
vote as an active current limit and a smart_charge flag as "set", which is wrong. The consumer side only works if business_charger_comp
dispatches events 84/85: the current source (G14k, in progress) and the G14k build do this exactly like the stock; any build that still
uses the pre-G14k business_charger_comp would silently drop the whole consumer side.

**CORRECTION (review-G23/mitee, 20.10.2026):** the verdict above is WRONG for the reviewed snapshot (buckchg md5 f4fe06752895): there was a
SECOND behaviour difference, in buckchg `strategy_buckchg_monitor_workfunc` (inlined select_charg_para). The stock reads `batt_emerg`
(info+0x920) once more there (`.text+0x2208 ldr [x19,#0x5bc] batt_missing; cbnz; .text+0x2210 ldr [x19,#0x6f0] = info+0x920; cbz`; x19 = work =
info+0x230), condition `batt_missing || batt_emerg` -> `ibus_limit = batt_type(info+8)==0 ? 500 : 1000`; that source tested batt_missing only
(D7 below; fixed in G23c, built .ko <work-dir> has the load). Effect on dada: see "What DOES act" and D7.

## The real event path (VERIFIED, stock)

1. `charger_partition_prepare` (boot work, only when `not_notify_module == 0`): `mca_strategy_func_process(6 FG, 85, 0)` before get_info_2
   (.text+0x1d88..0x1d94). Goes ONLY to fg_comp. If fg_comp has not registered its strategy ops yet, the call is lost (same in ours).
2. fg_comp `strategy_fg_process_event` case 85 (jump-table entry 85 -> .text+0x4200): zero `batt_emerg_sn[33]` (+5581),
   `charger_partition_read_batt_emerg_sn()` (result ignored), log, set `batt_emerg_update` (+5580) = 1, return 0. Event value ignored. Case 84 = default (no-op).
3. fg_comp `strategy_fg_monitor_workfunc` calls `mca_strategy_check_batt_emerg` (.text+0x34f0) every cycle; sysfs `batt_emerg` store
   (strlen(buf) must be exactly 32, no newline) copies the SN, sets the flag, writes the SN to the partition and calls the check directly.
4. `mca_strategy_check_batt_emerg` (.text+0x680c): flag==0 -> return. `platform_fg_ops_get_batt_sn(0, buf[33])` fails -> log, return,
   flag STAYS set (retry every cycle). strnlen(...,33) of both; ==33 -> fortify_panic; either length != 32 -> clear flag, nothing else.
   strncmp(32) != 0 -> `write_batt_safe_vlim(0)`, `write_batt_safe_ilim(0)`, clear flag. Equal -> read vlim and ilim; any read error ->
   flag = 1 (retry), return. vlim|ilim != 0 -> `batt_emerg_status`(+5576)=0, log, `mca_event_block_notify(4 HW_INFO, 84, &status)`.
   Both 0 -> status = 1, `mca_smartchg_set_batt_emerg_soc_limit(60)` (result ignored), `mca_event_block_notify(4, 85, &status)`. Clear flag.
5. HW_INFO (type 4) block notifier is registered ONLY by business_charger_comp (`business_charger_notifier_hw_cb`): bit mask
   0x70000000001f4 over (event-39) -> events 41,43..47,83,84,85 copy 4 bytes, 39 copies 1 byte. `business_charger_event_thread`
   (jump table .rodata+0x14): 84 -> `func_process(1 QUICK, 84, *(int*)d)`, `(8 SMART, 84, *(int*)d)`;
   85 -> v = *(u8*)d; `(0 BUCK,85,v)`, `(1 QUICK,85,v)`, `(4 REVCHG,85,v)`, `(2 BASIC_WLS,85,v)`, `(3 QUICK_WLS,85,v)`.
   FG is not a target, so there is no loop. 85 with value 0 never reaches the consumers (only FG gets 85/0 from charger_partition).
6. Consumers (all VERIFIED by jump tables + code):
   - smart_charge 84: if `support_batt_safe_lvl` (+0x67a): `batt_safe_active`(+0x678)=1, queue smart_charge_work now, log.
     workfunc (only while online): if active && support: read vlim from the partition once (static flag, only while cached vlim == 0,
     flag set only on success); if vlim >= 1: `mca_vote(smartchg_delta_fv /*MAX votable created by smart_charge*/, "batt_safe", 1, vlim)`.
   - quickchg 84: `batt_safe_active`(+0xc78)=1 + log (no DT check). regulation: if active && `support_batt_safe_lvl`(+0xc80): read ilim
     once into sysfs `cur_ratio`(+0xaa4) if it is 0; if 1 <= ratio <= 99 && cur_stage == 0:
     `mca_vote(info+0x30 /*curr_max_voter*/, "batt_safe", 1, ratio*jeita_fcc/100)`  -> see "no-op" below.
   - quickchg 85: `batt_emerg`(+0xc7c)=1, `mca_vote(quick_chg_disable /*+0x328, OR*/, "batt_emerg", 1, value)`; pre-charge check refuses to
     start while batt_emerg != 0 (the "force stop ... batt_emerg" path, .text+0x388c).
   - buckchg 85 (`strategy_buckchg_process_batt_emerg_change`): `batt_emerg`(+0x920)=1 (**CORRECTED G23c 20.10: it IS read, once: stock `strategy_buckchg_monitor_workfunc` .text+0x2210 `ldr w8,[x19,#0x6f0]` = info+0x920, inlined select_charg_para: `if (batt_missing || batt_emerg) ibus_limit = batt_type==0 ? 500 : 1000`; our source tested only batt_missing until G23c; units of 500/1000 not proven**),
     `mca_vote(input_voltage /*+0xe8, AND*/, "batt_emerg", 1, 0)`, `mca_vote(buck_input /*+0xf0, MIN*/, "batt_emerg", 1, 500)`. Value ignored.
     Where the monitor's 500/1000 goes (VERIFIED, stock disassembly): `mca_vote(target_volt==5000 ? buck_5v_in(+0x110) : buck_9v_in(+0x120),
     "wire_chg_type", 1, ibus_limit)` only when it differs from proc_data.ibus_limit (+0x164) or the voltage changed, then
     `set_restart_aicl(MAIN,1)`; callback set_5v_input/set_9v_input stores +0x130/+0x138 (read nowhere) and, if proc_data.voltage matches,
     votes `buck_input` "volt_limit"; buck_input (MIN) callback `strategy_buckchg_input_limit` -> `platform_class_buckchg_ops_set_input_curr_lmt(0, v)`
     (skipped if support_multi_buck; dada global DT: 0) -> ops+0xd0 = mca_qcom_subpmic_proxy `qcom_subpmic_set_input_current_limit` ->
     `mca_adsp_glink_write_prop(0x2008, &v, 4)`: the value reaches the ADSP unscaled. Restart AICL = glink prop 0x2014. dada global dtbo has no
     `batt_type` in mca_strategy_buckchg_class -> default 0 (probe `mov w3,wzr`) -> 500. Other modules: mca_charger_thermal only votes on
     buck_5v_in/9v_in; fg_comp reads buck_input's effective result only as `!= 0`.
   - basic_wireless 85: `mca_vote(info+0x78 = find_votable("buck_charge_curr") /*lazy, find_voter_work*/, "batt_emerg", 1, 500)`.
   - quick_wireless 85: `mca_vote(wls_quick_chg_disable /*+0x20, OR*/, "batt_emerg", 1, 1)` (constant 1).
   - wireless_revchg 85: log "batt emerg, disable vote", `mca_vote(usbin_rev_disable /*+0x8, OR*/, "batt_emerg", 1, 1)` (constant 1).

## (1) Data in charger_partition (VERIFIED)

- `struct ChargerPartition.info_1` at +0xa8, 68 bytes. vlim = u16 at +0xc8 (info_1+32), ilim = u16 at +0xca (+34), SN 32 bytes at
  +0xcc..+0xeb (+36..+67): `ldrh/strh` and the 4x8-byte copies in the six exports confirm the offsets in `stubs_portB_g` charger_partition_class.h.
- read_vlim/ilim: get_info_1 first, then pointer test, -1 on any error; write_vlim/ilim: return set_info_1's result; read_sn tests the
  pointer first; write_sn ignores set_info_1's result. Our source = stock (42/42 byte-identical per G14b; offsets re-checked here).
- Persistence: every write is read-modify-write of the whole 68-byte block (get_info_1 then set_info_1), no lock, no checksum, no range
  check; ints are truncated to u16, SN is not NUL-terminated on disk (fg_comp adds the NUL in a 33-byte buffer). Same in ours.
- Unprogrammed data (INFERRED, not seen on hardware): all-zero -> SN length 0 -> the check exits silently, nothing else happens.
  All-0xFF -> SN length 32, a 32-char battery SN never matches -> vlim/ilim are rewritten to 0 on every trigger (boot event 85 / sysfs);
  0xFFFF vlim/ilim are never consumed because the readers only run after event 84, which needs an SN match. Identical code in ours.

## (3) Limits that are requested but cannot take effect (stock behaviour; ours must and does behave the same)

| # | Where | Why it has no effect in the stock | Ours |
|---|---|---|---|
| N1 | smart_charge `mca_smartchg_set_batt_emerg_soc_limit(60)` -> `mca_vote(info+600 soc_limit_voter, "batt_emerg", 1, 60)` | +600 is only READ (.text+0x8b0); info is a static devm_kzalloc'd struct; no store to +0x258 in the module; `mca_vote(NULL)` returns -22 at `cbz x0` (mca_common .text+0x618). No event-85 consumer implements a SoC limit either. | same: field never assigned (MATCHES) |
| N2 | smart_charge `smartcharging_handle_algorithm_conflict`: `batt_emerg_soc_limit` (+0x679) branch (soc_upper_thre = 1) | +0x679 is read once (.text+0x11f4) and written nowhere -> dead branch | same: never written (MATCHES); comments wrong (D4) |
| N3 | quickchg regulation "batt_safe" vote = ratio*jeita_fcc/100 on info+0x30 (`curr_max_voter`) | NEW FINDING: +0x30 is never stored in the stock quickchg (7 find_votable results go to +0x8/0x10/0x18/0x20/0x28/0x348/0x358, the 8th ("smartchg_delta_ichg") is only used for an effective-result read; no "curr_max" lookup at all; no str/stur/stp of an x-register at #0x30 with any base; only memcpy targets the stack); info is devm_kmalloc(3208, GFP_KERNEL|__GFP_ZERO) -> `mca_vote(NULL)` = -22. So the whole quickchg safe-ilim / sysfs `current_ratio` feature only persists the number to the partition and never limits anything. | same: `curr_max_voter` never assigned (MATCHES); G14f text/comments say it limits (D3) |
| N4 | smart_charge DFX 63 `mca_get_client_vote(curr_max, "batt_safe")` | nobody votes "batt_safe" on curr_max (N3), param[2] is always the "no such client" value | same, plus D1 |
| N5 | basic_wireless 500 on lazily found `buck_charge_curr` | no-op while `strategy_wireless_find_voter_work` has not found the votable (NULL) | same mechanism (MATCHES) |
| N6 | whole consumer side | events 84/85 exist only via business_charger_comp's HW_INFO notifier; event 85/0 from charger_partition is lost if fg_comp is not registered yet | business: G14k source/build = stock (see D6) |

What DOES act in the stock when the SN matches: (vlim|ilim != 0) only the smart_charge delta-FV "batt_safe" vote (needs DT
`support_batt_safe_lvl`, only while online); (vlim = ilim = 0) quick charge disabled, buck input request forced to 5 V and input limit 500,
wireless buck_charge_curr 500 and wireless quick charge disabled, reverse charging disabled. The 60 % SoC limit does NOT act.
CORRECTION/addition (review-G23, 20.10): (a) the buckchg monitor ALSO overrides its per-type ibus request: "wire_chg_type" on buck_5v_in = 500
(batt_type 0) every tick while online and not quick charging; because buck_input is a MIN votable that already holds "batt_emerg" 500, this
changes the effective input limit only where the per-type value was BELOW 500: charger type UNKNOWN (100, stock jump-table entry 0 goes straight
to the batt_missing test with w24 = 100) and EU-model SDP (in_sdp-50 = 450 with dada in_sdp 500) are RAISED to 500; all other types stay at 500
via the batt_emerg vote; plus one restart-AICL request when the value changes. (b) "input limit 500" is not unconditional in the stock:
`strategy_buckchg_input_limit` raises any non-zero effective result below 1500 to 1500 while boottime < 60 s and smem reports zero-speed
(.text+0x5054..0x5080), and no callback runs while an override is active on buck_input (mca_vote skips the callback when +0x160 != -22;
quickchg holds overrides "icl_limit"/"pmic_temp_term" on it during quick charging). Both are stock behaviour, identical in ours.
(c) `batt_emerg` (+0x920) is never cleared (single store), so the monitor override lasts until module reload / reboot, like the votes.

## (4) Vote lifetime (stock vs ours)

| Client | Votable / value | Added | Removed (stock) | Ours |
|---|---|---|---|---|
| batt_safe | smart_charge `smartchg_delta_fv` (MAX), vlim | workfunc every 5 s tick while online+active+support+vlim>=1 | NEVER (only 2 refs to the string: vote + DFX read). sysfs `batt_safe_lvl=0` only clears the partition, not the vote nor the cached vlim/active flag | MATCHES |
| batt_safe | quickchg info+0x30 (NULL) | regulation | never (moot, no-op) | MATCHES |
| batt_emerg | smart_charge soc_limit_voter (NULL) | fg check | never (moot) | MATCHES |
| batt_emerg | quickchg `quick_chg_disable` (OR), value (1) | event 85 | NEVER (single string ref); `batt_emerg` field never cleared | MATCHES |
| batt_emerg | buckchg `input_voltage` (AND) 0 and `buck_input` (MIN) 500 | event 85 | NEVER | MATCHES |
| wire_chg_type (batt_emerg branch) | buckchg `buck_5v_in`/`buck_9v_in` (MIN) 500 (batt_type 0) / 1000 | every monitor tick after event 85 (same client as the per-type value) | never while batt_emerg = 1 (flag never cleared) | MISSING in the reviewed snapshot; MATCHES since G23c (D7) |
| batt_emerg | basic_wls `buck_charge_curr` (MIN) 500 | event 85 | NEVER | MATCHES |
| batt_emerg | quick_wls `wls_quick_chg_disable` (OR) 1; revchg `usbin_rev_disable` (OR) 1 | event 85 | NEVER | MATCHES |
| csd_pulse | buckchg `buck_charge_curr` 1500 | `process_csd_pulse(v!=0)` (+75 s work, 18750 jiffies) | process_csd_pulse(0), csd workfunc 1st pass, monitor (csd_flag && effective < 1500), stop_charging, soc-limit callback | MATCHES (G14e; release sites re-checked here) |
| csd_pulse | buckchg `chg_enable` disable | csd workfunc 1st pass | csd workfunc 2nd pass, process_csd_pulse(0), stop_charging | MATCHES |
| csd_pulse | quickchg `quick_chg_disable`, event value | event 54 (from business QUICK then BUCK; buck workfunc re-sends (QUICK,54,0) = enabled vote with value 0) | only USB_DISCONNECT (case 0, .text+0x2ef4) | MATCHES (`reset_charge_para`, called only from USB_DISCONNECT) |

All "never removed" votes last until module reload / reboot; after a reboot the boot-time event 85 re-arms the check, so the emergency state
re-appears while the SN stays stored and equal. This is the stock design; our code reproduces it (no "better" behaviour was added).

## (5) Units and meaning

- vlim: vote value on `smartchg_delta_fv` (MAX votable; same voter as "dtpt" 15 and sysfs SMARTCHG_FV val*cell_type) -> a float-voltage
  reduction; unit mV INFERRED. u16 on disk VERIFIED. Valid when >= 1 VERIFIED.
- ilim: ratio, 1..99 VERIFIED (`cmp #1; b.lt; cmp #0x63; b.hi`; sysfs >99 or negative -> 0), applied as `ratio*jeita_fcc/100`
  VERIFIED (smull 0x51eb851f, asr #37) = percent of jeita_fcc VERIFIED; jeita_fcc unit mA INFERRED. No effect (N3).
- 500 (buckchg `buck_input`, basic_wls `buck_charge_curr`): immediate 0x1f4 VERIFIED; mA INFERRED (same votables carry 1500 for csd).
  CORRECTION (review-G23): the csd 1500 is on `buck_charge_curr` only, not on `buck_input`. For buck_input / buck_5v_in the value is passed
  unscaled to ADSP glink property 0x2008 (VERIFIED, mca_qcom_subpmic_proxy ops+0xd0); dada DT in_sdp 500 / in_dcp 1500 / in_pd 1600 fit mA;
  the unit is still INFERRED (the ADSP side was not read). Monitor 500/1000: same path, same caveat.
- 0 on buckchg `input_voltage`: callback `strategy_buckchg_input_voltage` maps 0 -> 5000, else 9000 (`csel`, stored at +0x16c) VERIFIED;
  "mV" INFERRED.
- 60: SoC percent INFERRED (range check 1..100 VERIFIED); no effect (N1).
- 1 on the *_disable OR votables = "disable": INFERRED from names (votable type 2 = MCA_VOTE_OR VERIFIED from create args).
- batt_safe_lvl sysfs 0 = reset partition (ilim, vlim, SN), 1..4 = remember + DFX 63 with 3 params VERIFIED; meaning of the levels unknown.

## (6) Differences ours vs stock, and defects

| ID | Place | Status | Severity | Fix proposal |
|---|---|---|---|---|
| D1 | smart_charge `smart_charge_workfunc` | DIFFERS: stock runs `if (!curr_max_voter) curr_max_voter = mca_find_votable("curr_max")` on EVERY online tick (all branches of the batt_safe block jump to .text+0x2778); ours does it only inside `if (batt_safe_vlim >= 1)` (confirmed in the G14e built .ko: the `cbz`/`b.lt` skip the find). Effect: only DFX 63 param[2] when the probe-time lookup failed. | LOW | move the two lines out of the `if (info->batt_safe_active && info->support_batt_safe_lvl)` block, directly after it, before `strategy_class_fg_ops_get_cyclecount` |
| D2 | smart_charge `mca_smartchg_set_batt_emerg_soc_limit` | DIFFERS: stock returns 0 on every path (`mov w0, wzr` at .text+0x8e0); ours returns the mca_vote result (-22) or the raw limit. Only importer is fg_comp, which ignores it (no behaviour difference). STATUS claim "stock falls off the end" is wrong. | INFO | `mca_vote(...); return 0;` on all paths (likely makes the function byte-identical); correct STATUS |
| D3 | quickchg comments + STATUS + RESUME G14f entry | DOC: describe the "batt_safe" vote as a real curr_max limit; in stock and ours `curr_max_voter` (+0x30) is never assigned -> no-op | MEDIUM (doc; misleads any safety reasoning) | rewrite the comment at the regulation block and STATUS/RESUME: "no-op in stock (NULL votable), kept for equivalence". Do NOT add a find_votable (that would be "better" than stock) |
| D4 | smart_charge.h `batt_emerg_soc_limit` (+1657) comment "set by mca_smartchg_set_batt_emerg_soc_limit()", .c line ~598 comment "wins over everything", STATUS G14e "a battery-emergency SoC limit wins" | DOC: the field is never written in stock nor ours; branch is dead | LOW | fix the three texts ("never written in GLOBAL 0.7 stock: dead branch") |
| D5 | charger_partition.c comment at the event-85 call ("no battery emergency is pending") and strategy_fg.h `batt_emerg_status` comment ("SoC limit raised") | DOC: event 85/0 actually arms the SN check in fg_comp; the SoC limit is never applied | LOW | reword comments |
| D6 | business_charger_comp | Previously (G14f state, CN-based) events 84/85 were neither size-mapped in hw_cb nor dispatched -> the whole consumer side would be dead (HIGH). Current source (G14k) and `<work-dir>` (20:27): hw_cb mask and event_thread cases 84/85 equal to stock (order BUCK, QUICK, REVCHG, BASIC, QUICK_WLS; byte load for 85; csd case 54 QUICK then BUCK). Its STATUS still says "Event 85 is NOT among the changes of this module" (G14f section). | resolved in source; HIGH if an old build is used | finish G14k; never combine the global consumers with a pre-G14k business_charger_comp; correct the STATUS line |
| - | charger_partition exports/prepare | MATCHES (byte-identical 42/42, offsets re-verified) | - | - |
| - | fg_comp check / case 85 / sysfs batt_emerg / monitor call | MATCHES (strlen vs strnlen+fortify: same results, buffers always NUL-terminated) | - | - |
| - | smart_charge event 84, sysfs batt_safe_lvl/vlim, reset, DFX, vote | MATCHES | - | - |
| - | quickchg 84/85/54/USB_DISCONNECT, pre-charge stop, regulation block, sysfs current_ratio | MATCHES | - | - |
| D7 | buckchg `strategy_buckchg_monitor_workfunc` (inlined select_charg_para) | DIFFERED in the reviewed snapshot (this review said MATCHES: WRONG): the `|| batt_emerg` reader (stock .text+0x2210) was missing; effect on dada after event 85: UNKNOWN-type 100 and EU-SDP 450 stayed below the stock 500, no restart-AICL. Fixed in G23c (`if (info->batt_missing \|\| info->batt_emerg)`), built .ko: same load/branch sequence as stock (VERIFIED by listing, review-G23) | LOW (only raises a limit to the stock 500) | done; never combine the pre-G23c buckchg with this chain |
| - | buckchg 85 (process_batt_emerg_change), csd_pulse paths | MATCHES | - | - |
| - | quick_wireless / basic_wireless / revchg case 85 | MATCHES (source level; offsets +0x20 / +0x78 / +0x8 and votable names verified in stock) | - | - |

## Not verified

- Nothing on hardware: real partition contents (zero vs 0xFF tail after CN firmware), real battery SN format, timing of the boot event 85.
- "Never written" for +0x30 (quickchg), +600 and +0x679 (smart_charge) rests on a search of all store forms with that immediate offset;
  aliasing through a differently offset base register is not exhaustively excluded (same caveat as G14f).
- mca_vote semantics for an enabled vote with value 0 on OR votables (quickchg "csd_pulse" after (QUICK,54,0)) not analysed in mca_common.
- Remaining differing functions of quick/basic_wireless, revchg, quickchg (monitor_work, div4 cb) were not re-read; jeita set_dynamic_fv,
  bq27z561 SN read and mca_charge_mievent (event BattSafeLvl) not re-checked here.
- G14k was still running: business_charger_comp / quickchg / quick_wireless may change after the md5 snapshot above.
