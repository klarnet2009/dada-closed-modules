# Stub inventory of the dada closed-module sources (goal-G1q2, 18.10.2026)

Scope: every module in `work/re/src/` whose STATUS.md first line is not EQUIVALENT (COMPILES-only / PARTIAL / BLOCKED). Nothing was fixed here, this is an inventory.

Method (honest limits):
1. STATUS.md first line (what the last ). 2. Grep of the module sources (without `stubs_*` provider dirs) for `stub|TODO|FIXME|placeholder|not implemented|barrier_data()`: most hits are upstream TODOs (tfa98xx, mi_damon, mibbr, minet, sia91xx, binder headers), the real ones are listed below.
3. Size scan (`work/re/src/g1q2_tools/stubinv.py`, run on the server): function SYMBOL SIZES of the newest built `.ko` of any agent (`<work-dir>`, snapshot 18.10.2026 ~17:30, some come from builds other agents had just finished) against the OS4 stock `.ko`: "missing" = stock function with no counterpart, "size-stubbed" = ours < 40 % of the stock size (stock >= 32 B). This catches stubs and missing code, it does NOT catch wrong-but-same-size bodies (those are counted in the module STATUS as "differs"), and it does not run compare_ko_v2 per module (not possible within this task's budget). Bytes = stock bytes with no real recovered counterpart.

## Ranked table (most unrecovered stock behaviour first)

| # | module | status (first line) | stock fn / bytes | missing or stub (scan) | what is stubbed / unrecovered |
|---|--------|---------------------|------------------|------------------------|-------------------------------|
| 1 | bq27z561 | BLOCKED (headers + source drift, no OS4 build) | 207 / 106.7 KB | all (nothing compiled against OS4) | whole fuel-gauge driver (5387-line older source does not match OS4) |
| 2 | mca_strategy_quickchg | BLOCKED (scaffold, does not compile) | 83 / 52.8 KB | all | whole quick-charge strategy (4220-line scaffold; 2 upstream "todo: multi battery") |
| 3 | mca_basic_wireless | BLOCKED (triage only) | 73 / 40.1 KB | all | whole basic wireless charging strategy (3996-line template that does not compile) |
| 4 | mca_strategy_buckchg | BLOCKED (scaffold) | 61 / 29.6 KB | all | whole buck-charge strategy (OS4 has 3 functions more than HyperOS 3) |
| 5 | mca_quick_wireless | BLOCKED (scaffold, OS4 event set differs from the template) | 37 / 23.0 KB | all | whole quick wireless strategy |
| 6 | mca_smart_charge | BLOCKED (scaffold, 7 exports) | 35 / 14.1 KB | all | whole smart-charge module (1433-line scaffold) |
| 7 | mitee_dlkm | PARTIAL (81 of 164 present functions identical) | 182 / 51.9 KB | 18 missing + 2 tiny stubs = 4.7 KB | Xiaomi TUI part (`mitee_tui.c` is a documented STUB: mitee_tui_init 724 B -> 8 B, tui_ioctl 632 B, tui_process_tuisvc_callback 620 B, tui_ioctl_tui_shm_alloc 332 B, send_cmd_to_user 324 B, tui_poll ...); also `TODO: add dts` in core.c. Security-relevant (TEE): never load |
| 8 | tfa98xx_dlkm | COMPILES-only (function set 389 = 389) | 387 / ~165 KB | 4 size-stubbed = 1.4 KB (tfa_dev_mtp_set 852 -> 340 B, tfa_dsp_msg_id 564 -> 216 B, tfa_dev_stop 504 -> 184 B, tfa98xx_powerdown 324 -> 128 B) | the "g3k HACK" ADSP calibration transport (stubs in tfa98xx.h), Oplus extras, IELP0 FIXME; the rest was never strict-compared (COMPILES-only) |
| 9 | aw882xx_dlkm | COMPILES-only (267 = 267) | 267 / - | 0 by size | bodies never compared function by function; `set_smartpa_codec` prototype is a GUESS (stub provider stubs_a17C); no .BTF: layouts from disassembly |
| 10 | sia91xx_dlkm | COMPILES-only (104 = 104) | 104 / - | 0 by size | `sipa.c:1910` GUESS for set_smartpa_codec argument (stock passes &client->dev); bodies not compared except sipa_i2c_probe / sipa_pending_actions rewritten |
| 11 | Wi-Fi qca_cld3_{kiwi_v2,peach,peach_v2,qca6750,wcn7750} | PARTIAL 17880-18289 of 18.3-18.4 k functions (peach_v2 157, kiwi_v2 158, wcn7750 153, qca6750 141, peach 444 differ) | 15.9-18.5 k each | 0 missing (set complete) | built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn + reconstructed Xiaomi additions, not Xiaomi's tree: 141-444 bodies differ per module (+ the documented `__LINE__` alignment hack, 17404 identical without it for peach) |
| 12 | mi_low_lat_mem | PARTIAL (84 of 110 identical, ThinLTO emu) | 111 / - | 0 | 26 bodies differ; forged `.llvm.<hash>` global, genksyms type pad for low_lat_mem_kobject, raw mci/dfx blobs, opaque placeholders in `llm_common.h` for typed mci/dfx structs; `mi_smart_refill_dynamic_page_pool_refill` prototype from a call site. Memory module: review before any load |
| 13 | mi_game | PARTIAL (157 of 234 identical, ThinLTO emu; no stub left) | 239 / - | 0 | 77 bodies differ (layout/RA); `kleaf_stubs.c` is an empty guard file now. Scheduler/game module: review before any load |
| 14 | matt | PARTIAL (all 261 functions written, 174 identical ThinLTO emu; Kleaf lto=none 87 differ) | 260 / - | 1 size-stubbed: matt_inq_free 128 -> 32 B (96 B) | matt_inq_free body shorter than stock (check semantics, maybe inlined list free); NOT hardware tested |
| 15 | mca_strategy_fg_comp | PARTIAL (79 of 97) | 97 / - | 0 | 18 bodies differ (layout); community-only |
| 16 | mca_connector_antiburn | PARTIAL (12 of 17) | 17 / - | 0 | `monitor_workfunc` + `probe` rewritten with codegen-form diffs; community-only |
| 17 | mca_pwm_fan / mca_buckchg_jeita / mca_business_charger_comp / mca_wireless_revchg | PARTIAL (16/20, 13/17, 41/45, 43/45) | 20/17/45/45 | 0 | layout-only diffs in 2-4 functions each; community-only |
| 18 | lb | PARTIAL (38 of 57 bodies) | 57 / 29.6 KB | 0 | 19 bodies differ |
| 19 | cameramsger | PARTIAL (14 of 22) | 22 / 14.5 KB | 0 | 8 bodies differ |
| 20 | mi_schedule / miloadtrace | PARTIAL vs OS3 only (90/100, 74/83) | no OS4 stock | not measurable | modules vanished in OS4, no reference to finish against |
| 21 | miscore, process_monitor, mist, rsmc_driver, dcvs_main, sched_entity_freq, mi_reclaim, ufs_ffu, mibbr, minet, dump_binder, fault_report, cameralog, rss_monitor, mi_damon, minidump | PARTIAL, 1-5 functions differ each (layout/RA, minet: 2 hidden functions carry a kCFI preamble the stock lacks) | - | 0 | no stubbed behaviour found; `mi_reclaim.c:356` comment "empty placeholders" is stale (STATUS: all 30 functions have real logic, 27 identical); ufs_ffu: firmware blobs zero-filled placeholder by design (never publish) |

Not in the list: xr_qi, xr_qs (no stub left, see their STATUS: 0 stubs; xr_qi 21 and xr_qs 6 bodies differ by codegen), EQUIVALENT modules, and modules whose OS4 name differs (sc8581 -> sc8581_sy6541 EQUIVALENT, sia91xx_tuning / tas25xx / typec_analog_acc: HyperOS 3 names).

## Reading the table

* The only real "behaviour missing" classes are rows 1-7 (whole modules or the TUI half of mitee) plus the 4 size-stubbed tfa98xx functions and matt_inq_free. Everything else is byte-layout work.
* All charging/fuel-gauge rows (1-6, 15-17) and xr_qi/xr_qs are community-only: never flash, never in a ROM build.
* Cheapest real gain per effort: matt_inq_free (96 B), tfa98xx 4 functions (1.4 KB, data from OS4 stock + public TFA98xx tree), mitee_tui (4.7 KB, the TUI protocol is partly public in the XRING tree), then mca_smart_charge (14 KB, smallest blocked charger module).
* Caveat: the size scan uses whichever built `.ko` was newest on the server at scan time; if an a trimmed variant last, "missing" can be overstated for that module (the scan printed the build directory in its output for traceability: `/tmp/g1q2_stubinv.txt` of the scan session).
