# dada-closed-modules

Reverse-engineered and ported source code for the closed-source kernel modules of the **Xiaomi 15 (codename `dada`, Snapdragon 8 Elite / SM8750)**.

Xiaomi's stock kernel for this phone loads roughly 630 GPL-licensed modules, but for many of them no public source exists. That makes it hard to build custom ROMs and kernels with full control over the device. This repository collects what we could recover. It is a research artifact, not a drop-in replacement for the stock modules: read "Read this first" before you load anything.

## Read this first

- **Nothing here has been tested on a device.** Not one module was loaded, run or flashed, by anyone involved in this work. Every claim below is a static, binary-level comparison of a module built from these sources against the stock `.ko` (build machine only). "Equivalent" is not "works", and "PARTIAL" means exactly what it says: some functions are known to differ and some of the differences were only read, not proven harmless.
- **`bq27z561` (battery fuel gauge) - READ BEFORE USING ANYTHING FROM THIS REPOSITORY.** The source in `ported/bq27z561/` contains a **real data-flash write path** (record-voltage levels written with MAC commands `0x448A` / `0x440C`, preceded by an unseal step and followed by a re-seal) and the **unseal procedure of the gauge**, reconstructed from the stock binary. The unseal key is **not in the source**: it is read at probe time from the device-tree property `unseal_key` into a 4-byte field; no key value, no data-flash image and no gauge firmware are published here (`docs/EXPORT_PRIVACY.md`). The code is PARTIAL (not strict-equivalent), has never run on a gauge, and a wrong data-flash write can permanently damage a battery pack or its protection logic. It is published **only to be read and reviewed**. Do not build it into a ROM, do not load it, do not flash it. 20 stock functions (firmware / data-flash / OTA / cloud / some gauge writes) are deliberately absent; the paths that are present were gated like the stock one, but that is a reading of disassembly, not a test.
- **Do not load or flash these modules (use the stock binaries): `matt`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `xr_qi`, `xr_qs`, (`lb` and `cameramsger` are not for a ROM either), `mitee_dlkm` (never `rmmod` it, even the stock one is unsafe to unload), `aw882xx_dlkm`, `sia91xx_dlkm`, `tfa98xx_dlkm`, and the whole charging / fuel-gauge family** (reconstructed and ported `mca_*`, `mca_business_*`, `mca_strategy_*`, `mca_smart_charge`, `mca_quick_wireless`, `mca_basic_wireless`, `mca_connector_antiburn`, `mca_wireless_revchg`, `mca_buckchg_jeita`, `mca_pwm_fan`, `bq27z561`, `nuvolta_1652`, `sc8581_sy6541`, `hl7603`, `charger_partition`, ...). **The charging family is community-only: published to read and review, never to be flashed or put on a real battery.** A wrong charging or gauge driver can damage a battery. **None of the rewritten charging modules is strict-equivalent** (they are PARTIAL; seven independent reviews and audits of the charging family plus targeted reviews of single chains (battery-emergency chain, `bq27z561` probe and dynamic-FV seal, fuel-gauge compensation monitor) are in `docs/reviews/`; each of them found real differences, which were then fixed; every module was re-measured on 20.10.2026 against the GLOBAL OS4.0.0.7 stock with the polarity-aware symbolic tool `tools/review5/symcmp5.py`, and for several functions - for example `mca_quick_charge_regulation`, `mca_strategy_quickchg` `monitor_work`/`process_event` and the buck-charging `monitor_workfunc` of `mca_buckchg_jeita` - the result is still INCONCLUSIVE or not read line by line: see the `STATUS.md` of each module, the first line is the truth). The fuel-gauge firmware-update and data-flash functions of `bq27z561` are deliberately **not** ported. Whether branches gated on the board's `platform_version` run on a `dada` (they should not: project 2) is read from the binaries and still to be verified on the device. **No safety claim of any kind is made for any module of this family.** `sia91xx_tuning_dlkm` is a different module from `sia91xx_dlkm`.
- The Wi-Fi drivers `qca_cld3_*` are PARTIAL research code: 18585 of 18601 functions of the dada variant (`peach_v2`) are byte-identical to the stock with the documented per-statement `#line` pins (about 17826 without them); every remaining differing function has a verdict, but for seven of them (`wlan_hdd_update_wiphy`, `lim_fill_pe_session`, `cm_process_join_req`, `target_if_direct_buf_rx_module_register`, `wlan_hdd_cfg80211_vendor_add_scan_entry`, `__qdf_nbuf_get_tso_info`, `wlan_hdd_cfg80211_do_acs`) the evidence is a differential emulation with 75-96% instruction coverage and no divergence, not a block-by-block proof. Several real behaviour differences were found and fixed in the last rounds, so more may exist; nothing ran on hardware (`docs/WIFI.md`). A read-only investigation of a roaming complaint found no roaming bug in the driver: the firmware roams, and the thresholds come from the Xiaomi ini (`docs/WIFI.md`, `docs/reviews/wifi_roaming_investigation.md`).
- The old comparison tool (`tools/compare_ko.py`) has blind spots: it does not look at static data, tables, bug-table lines or the kCFI type hash in front of each function. The strict tool is `tools/compare_ko_v2.py` together with `tools/preamble_cmp.py` (kCFI); see `docs/VERIFICATION_METHODS.md`. Of the 103 modules marked EQUIVALENT, 99 were re-checked strictly (last column of the tables), the rest (4) only with the old tool. Even the strict tool missed real differences in the Wi-Fi drivers until an independent review dumped the objects it had waved through.
- This is reverse-engineered code. Several modules contain documented build hacks (forged export/import CRCs, stub providers, `#line` pins, codegen hints, a 6.6.57 header emulation): `docs/HACKS_AND_FORGED_CRCS.md`. `xm_compat_6657.h` is an emulation of old kernel headers for the build, **not Xiaomi code**.
- The stock modules were built against kernel 6.6.57 headers, ours against 6.6.144: part of the remaining differences (inlined `readl`, `netif_tx_stop_queue`, bug-table line numbers, an extra barrier in `poll_wait`, some export CRCs; a BTF audit found no shifted field that a stock module reads, `docs/reviews/struct_layout_audit.md`) come from that drift, not from errors in the source (`docs/STATUS_AND_LIMITS.md` section 9).
- Reviews found bugs in the **stock** `matt` module, two of them remote (S1: out-of-bounds read of SACK blocks, S2: use-after-free read in the receive path); our `matt` source contains two deliberate **hardenings** that deviate from the stock (S1/S2, listed in its `STATUS.md`), the other stock bugs are reproduced on purpose. Reviews also found bugs in the stock Wi-Fi driver (P2P header-compression parsers, a vendor ACS command): `docs/STATUS_AND_LIMITS.md` section 7.
- Not affiliated with, or endorsed by, Xiaomi or Qualcomm.

## Status in numbers

146 modules are tracked in this repository (state 23.10.2026):

| status | modules |
|---|---|
| EQUIVALENT | 103 |
| PARTIAL | 43 |
| COMPILES-only | 0 |
| BLOCKED | 0 (none) |

(3 modules appear twice, once under their HyperOS 3 name and once under their HyperOS 4 name: `tas25xx`, `typec_analog_acc`, `sia91xx_tuning`.)

Since the previous update (commit 2253f2e, 19.10.2026) **no module changed its status class** and one module was added (`swinfo`, EQUIVALENT, 3 functions; its platform-configuration header is not published). What changed is the reference and the depth of the checking: most modules were re-measured against the **GLOBAL HyperOS 4.0 stock (`OS4.0.0.7.XOCMIXM`)** instead of the CN beta (`OS4.0.0.6`); the "compared against" column says which. Inside PARTIAL the numbers moved (for example `bq27z561` 144 of 200 common functions identical, was 119 of 182; `mitee_dlkm` 156 of 182, was 151; `matt` 178 of 261 in a real build, was 174 in an emulation; `xr_qi` 350 of 369; `xr_qs` 113 of 118), and a polarity-aware symbolic comparison (`tools/review5/`) was run over the charging family, `lb`, `mi_game`, `xr_*`, `cameramsger`, `mi_reclaim` and the Wi-Fi drivers. A final round on 20.10.2026 re-read every remaining differing function of the charging family, `bq27z561`, `xr_qi`, `xr_qs`, `mi_game`, `mi_low_lat_mem`, `mitee_dlkm`, the audio drivers and the Wi-Fi tails from raw listings and found and fixed a further set of real behaviour differences (`CHANGELOG.md`; for example a quick-charge table row, the order of votes in one inlined charging branch, a `batt_emerg` condition term, wrong register writes of three audio drivers, `current->active_mm` in `mitee_dlkm`, the caller task in a binder hook of `xr_qi`); the audio drivers moved to 252 of 267, 76 of 104 and 313 of 389 identical functions (`aw882xx_dlkm`, `sia91xx_dlkm`, `tfa98xx_dlkm`), `xr_qi` stays at 350 of 369, `mi_game` at 179 of 245. New independent reviews (seventh charging review, battery-emergency chain, `bq27z561` probe and seal helpers, `matt`, `mitee_dlkm`, camera/`lb`, `mi_reclaim`, `mi_game`, fuel-gauge compensation monitor, Wi-Fi tables) found real differences that were fixed; some items are still open and listed below. A last round on 22-23.10.2026 re-read every remaining differing function of every module from raw listings (several by an independent second reader), found and fixed about sixty more real behaviour differences (see `CHANGELOG.md`, top section: for example an endless `cmpxchg` loop in the `matt` skb destructor, a `mi_game` flag written to the wrong word so that a work item never ran, a Wi-Fi flag written to the wrong structure, three signed-overflow comparisons in the quick-charge code, the revision table of `tfa98xx`) and ended with a verdict for every differing function, with the seven Wi-Fi functions above as the only ones without a block-level proof. The number of EQUIVALENT modules is 103 (it was 100; the difference is `swinfo`). The charging code is community-only; its numbers are in `docs/STATUS_AND_LIMITS.md` section 5, and `CHANGELOG.md` has the details.

What the statuses mean (all against the stock module of HyperOS 4.0 where the table says so):

- **EQUIVALENT**: function bodies, relocations, exported/imported symbols with CRCs and modinfo match. Where the strict column says "strict-equivalent" or "differs in metadata only", `tools/compare_ko_v2.py` also compared the content of every data object and table (the remaining differences are `intree=`/`scmversion=`/`built_with=DDK` in modinfo, the LTO note section, `depends=` and, in a few modules, line numbers in the bug table).
- **PARTIAL**: same interfaces and logic as far as checked, but some functions differ (the `STATUS.md` of a module says how many, which of them are only codegen (register allocation, block layout) and which are not proven; a module with open items is still PARTIAL, never called equivalent) (register allocation, block layout, and in some modules real code differences or template code that the STATUS.md of the module lists). Per-module counts of identical functions are in each `STATUS.md`.
- **COMPILES-only**: builds against 6.6.144 and the set of functions equals (or nearly equals) the stock one, but many function bodies differ; not a usable replacement.
- **BLOCKED**: does not compile yet; see `docs/blocked/<module>.md`.

## What is in the repository

- **`reconstructed/`**: 26 modules that have no public source in any place we looked (all 267 branches of Xiaomi's public MiCode repositories, the OnePlus and LineageOS Qualcomm trees). Their C source was rebuilt from the stock `.ko` binaries (first from HyperOS 3.0.6, `OS3.0.6.0.WOCMIXM`, then re-checked and corrected against HyperOS 4.0, `OS4.0.0.6.XOCCNXM`: the table says which) and compiled against a GKI 6.6.144 tree with ThinLTO.
- **`ported/`**: 120 modules whose source exists in older public Xiaomi trees (MiCode `*-oss` branches, XRING `xiaomi-modules` including the `mitee` TEE driver, OnePlus), or that were written from scratch from the binary (for example `matt`, `mi_game`, the Wi-Fi extension). Ported to 6.6.144 and compared the same way (87 EQUIVALENT, 33 PARTIAL). Original copyright headers are kept. 119 of them were compared against the HyperOS 4.0 (Android 17) stock modules. `ported/qca_cld3_*` hold only our notes (the driver tree is the public Qualcomm one), `ported/qca_cld3_xiaomi_ext/` our Xiaomi extension code and `ported/qca_cld3_recipe/` the assembly scripts.
- **`docs/blocked/`**: the notes (`STATUS.md`) of the modules that do not compile yet, with what exactly is missing (present only while such a module exists: none at the moment).
- **`stubs/`**: tiny stand-in provider modules and header sets (several of them generated from the stock BTF) used only to satisfy export/import CRCs and types when a module is built on its own. They are not modules to load. The forged `os4_*` provider modules that `mitee_dlkm` needs are not published.
- **`tools/`**: (new since the last update: `review5/` with the polarity-aware symbolic comparison `symcmp5.py`/`symcmp5b.py` and its self-test, and more helper scripts in `charging/` and `rounds/`) `compare_ko.py` (first-generation check), **`compare_ko_v2.py`** (strict check), `preamble_cmp.py` and `verify_pkg.py` (kCFI hashes, `depends=`, vermagic, CRCs against `Module.symvers`, load order), `crctool.py`, `crc_patch.py`, `crc_patch2.py`, `modinfo_fix.py`, `btf_dump.py`, `btf_dump_startid.py`, `patch_btf.py`, `gen_stubs.py` (stub providers with exact export CRCs), `padcrc.py`/`crcpad.py`, `gen_trace.py`, `payload_extract.py`, `vendor_boot_extract.py`, and the sub-directories `review/` (function fingerprints, data/table comparison), `review3/` (symbolic comparison of calls, stores and conditions), `review4/` (`symcmp4.py`: the same with the guards of every effect, used by the hidden-behaviour audit), `matt_review/` (register-agnostic comparison of one module, kCFI checks), `multiset/` (instruction multiset diff, unity build), `kcfi/` (kCFI hash to prototype search, with Itanium substitutions), `rounds/` and `charging/` (per-round helper scripts). Several scripts were written for our own build server and contain placeholders such as `<work-dir>` where a path was; they document the method more than they run unchanged.
- **`docs/`**: `STATUS_AND_LIMITS.md` (what is and is not proven, the do-not-load list, the charging family, stock bugs, kernel drift), `VERIFICATION_METHODS.md` (compare_ko_v2, kCFI, multiset diff, BTF), `HACKS_AND_FORGED_CRCS.md`, `KERNEL_PACKAGE.md` (what a full module set built from this work looks like, in numbers), `WIFI.md` and `wifi/` (the Wi-Fi rounds: notes, behaviour specs, patches), `EXPORT_PRIVACY.md` (what is deliberately not published), `reviews/` (independent reviews of the charging family (seven) and of single chains, the all-function audits of the final round, the struct-layout audit, `matt`, `mitee_dlkm`, `mi_reclaim`, `lb`/camera, `mi_game`, the Wi-Fi drivers and their tables, the polarity audits, the global-stock status reports; `reviews/untranslated/` holds working reports that are partly in Russian; the hidden-behaviour audit of the charging family, the Wi-Fi roaming investigation, the multiset audits, the charging enum-name audit, the stub inventory), `METHOD.md`, `LESSONS_round2.md`, `LESSONS_round3.md`, `LESSONS_round4.md` and `source-map/` (which module has source where).

## Open items: every PARTIAL and COMPILES-only module (first line of its STATUS.md, shortened)

| module | status | first line of `STATUS.md` |
|---|---|---|
| cameralog | PARTIAL | cameralog - PARTIAL (10 of 11 functions byte-identical vs the OS4 stock, re-checked by g4 17.10.2026) |
| cameramsger | PARTIAL | cameramsger - PARTIAL (14 of 22 functions byte-identical; each of the other 8 has a block-level EQUIVALENT-PROVEN verdict in G55a, see below), NOT hardware-tested, not for a ROM: the independent review gate is met (G20a); a dmesg-watched test load (camera open/close, cam_msger reg/snd, no BRK/UBSAN) is the only open step |
| lb | PARTIAL | lb - PARTIAL (40 of 57 function bodies byte-identical; each of the other 17 has a block-level EQUIVALENT-PROVEN verdict in G55a, see below), NOT hardware-tested, not for a ROM: the independent review gate is met (G20a); a dmesg-watched test load (no BRK/UBSAN) is the only open step |
| mca_business_charger_comp | PARTIAL | mca_business_charger_comp - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 3 of 45 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 4 of 45; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_strategy_fg_comp | PARTIAL | mca_strategy_fg_comp - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 19 of 99 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 19 of 99; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mi_schedule | PARTIAL | mi_schedule - PARTIAL (90 of 100 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026); G57a: kCFI 92/92 (mi_group_ls_group signature fixed); G59a: the last 2 OPEN functions (runtime_show_all, package_runtime_show) now EQUIVALENT-PROVEN from the raw listings, so every non-identical function has a verd ... |
| mibbr | PARTIAL | mibbr - PARTIAL (26 of 28 functions byte-identical; mibbr_main and update_bbr_uid_list have block-level EQUIVALENT-PROVEN verdicts in G55a; function set, relocations, objects, imports + CRCs, modinfo, kCFI all OK; bug-table lines: 4 kernel-header lines differ by tree version), NOT hardware-tested |
| miloadtrace | PARTIAL | miloadtrace - PARTIAL (74 of 83 functions) vs OS3 only; no OS4 counterpart (module vanished, g5 check 17.10.2026); G57a: mi_gov_update_freq double rounding fixed; G59a (23.10.2026): 4 more real source differences fixed (mi_gov_update_freq out-of-capacity max_limit + 3 width issues, mi_frame_ioctl loop NULL test), the 3 OPEN func ... |
| mist | PARTIAL | mist - PARTIAL (67 of 70 functions byte-identical; the other 3 have block-level EQUIVALENT-PROVEN verdicts in G55a), NOT hardware-tested |
| process_monitor | PARTIAL | process_monitor - PARTIAL (38 of 42 functions byte-identical; the other 4 have block-level EQUIVALENT-PROVEN verdicts in G55a), NOT hardware-tested |
| aw882xx_dlkm | PARTIAL | aw882xx_dlkm - PARTIAL (253 of 267 functions byte-identical vs the GLOBAL OS4.0.0.7 stock; each of the other 14 has an EQUIVALENT-PROVEN verdict in G53a, 0 OPEN; function set, kCFI 235/235, relocations of the identical functions, 87 objects, anon data, tables, 88 imports + CRC, modinfo all PASS in compare_ko_v2), NOT hardware-te ... |
| bq27z561 | PARTIAL | bq27z561 - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 54 of 200 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 56 of 200; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| dcvs_main | PARTIAL | dcvs_main - PARTIAL (15 of 18 functions byte-identical vs the CN OS4.0.0.6 beta stock - no GLOBAL OS4.0.0.7 module exists; the other 3 have block-level EQUIVALENT-PROVEN verdicts in G55a; exports + CRCs all OK; no public source, reconstructed from the disassembly), NOT hardware-tested |
| dump_binder | PARTIAL | dump_binder - PARTIAL (10 of 11 functions byte-identical; the 11th has a block-level EQUIVALENT-PROVEN verdict in G55a; function set, relocations, objects, 0 exports, 38 imports+CRC, modinfo all OK in the Bazel compare) |
| fault_report | PARTIAL | fault_report - PARTIAL (7 of 8 functions byte-identical; get_stack_info has a block-level EQUIVALENT-PROVEN verdict in G55a; function set, relocations, objects, 39 imports+CRC, modinfo OK in the Bazel compare) |
| matt | PARTIAL | matt - PARTIAL (G22a 20.10.2026 on top of G15d 19.10.2026 [G22a: review D1 + D2 fixed, a REAL loadable Kleaf .ko exists (md5 011c62d9..., see G22a below), D5 checked against kernel 6.6.144: 0 kCFI problems; numbers below are the G22a build], base GLOBAL OS4.0.0.7 stock: stock matt.ko is code-identical to CN 0.6; real Kleaf lto=n ... |
| mca_basic_wireless | PARTIAL | mca_basic_wireless - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 7 of 73 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 9 of 73; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_buckchg_jeita | PARTIAL | mca_buckchg_jeita - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 3 of 17 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 4 of 17; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_connector_antiburn | PARTIAL | mca_connector_antiburn - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 5 of 17 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 5 of 17; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_pwm_fan | PARTIAL | mca_pwm_fan - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 4 of 20 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 4 of 20; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_quick_wireless | PARTIAL | G60a (23.10.2026) mca_quick_wireless - PARTIAL, DO NOT LOAD, community-only (never flash, never in a ROM build or the kernel package), NO safety claim, NOT hardware-tested; GLOBAL OS4.0.0.7 stock base. NOT STRICT: buildmeta/depends forged. 3 of 37 functions differ in bytes (unchanged); G60a closed the last OPEN one: mca_wireless ... |
| mca_smart_charge | PARTIAL | G51a (22.10.2026) mca_smart_charge - PARTIAL, DO NOT LOAD, community-only (never flash, never in a ROM build or the kernel package), NO safety claim, NOT hardware-tested; GLOBAL OS4.0.0.7 stock base. NOT STRICT: buildmeta/depends forged. 5 of 38 functions differ in bytes (same set as before); every one has a verdict, none. .ko m ... |
| mca_strategy_buckchg | PARTIAL | mca_strategy_buckchg - PARTIAL, DO NOT LOAD, community-only (G52a 22.10.2026: 10 of 62 functions differ from the GLOBAL OS4.0.0.7 stock after a real Kleaf rebuild, was 11 of 62; NOT STRICT-EQUIVALENT; never flashed, never in a ROM build or the kernel package, NOT hardware-tested, NO safety claim) |
| mca_strategy_quickchg | PARTIAL | G60a (23.10.2026) mca_strategy_quickchg - PARTIAL, DO NOT LOAD, community-only (never flash, never in a ROM build or the kernel package), NO safety claim, NOT hardware-tested; GLOBAL OS4.0.0.7 stock base. NOT STRICT: buildmeta/depends forged. 25 of 83 functions differ in bytes (same set as fix7, no new differing function, 58 ide ... |
| mca_wireless_revchg | PARTIAL | G51a (22.10.2026) mca_wireless_revchg - PARTIAL, DO NOT LOAD, community-only (never flash, never in a ROM build or the kernel package), NO safety claim, NOT hardware-tested; GLOBAL OS4.0.0.7 stock base. NOT STRICT: buildmeta/depends forged. 2 of 45 functions differ in bytes (same set as before); every one has a verdict, none. .k ... |
| mi_game | PARTIAL | mi_game - PARTIAL, DO NOT LOAD, base = GLOBAL HyperOS OS4.0.0.7 (G48a 20.10.2026: three review-g40f findings fixed and rebuilt, md5 39d66e8e48ed26f70f02824495a753f5: mist_frame_load_history_update [inlined update_avg_freq: the count variable is reused like the stock, so cluster j>=1 computes sum_history_margin with the count of  ... |
| mi_low_lat_mem | PARTIAL | mi_low_lat_mem - PARTIAL, DO NOT LOAD, GLOBAL OS4.0.0.7-BASED (G48a 20.10.2026: two review-g40f findings fixed and rebuilt, md5 a9ef844c84cf1ffa8211015a9d3fcfa3: mi_pre_compact_init clears mci+0x30 after the kfree on the pool(9) kmalloc failure, pre_do_compact keeps pct4 = pct9 = 100 on the success exit; function-bytes still 28  ... |
| mi_reclaim | PARTIAL | mi_reclaim - PARTIAL, DO NOT LOAD (memory-reclaim module, NOT hardware-tested; G22b 20.10.2026: checklist items 2-5 and D2 closed on paper/build: the ROM kernel package carries the STOCK mi_reclaim.ko (ours is not in it), load order xr_qi < mi_mempool < mi_reclaim confirmed, build against a provider named xr_qi gives depends=xr_ ... |
| miscore | PARTIAL | miscore - PARTIAL (24 of 27 functions byte-identical (G57a: input_hook4/6 fixed); function set, relocations, objects, 0 exports, 36 imports+CRC, kCFI 25/25, modinfo all OK in the Bazel compare; 3 functions differ by codegen only; no hardware test) |
| mitee_dlkm | PARTIAL | mitee_dlkm - PARTIAL (real Kleaf/ThinLTO build compiles+links; compare_ko_v2 vs OS4 stock: function-set PASS 182 = 182, kCFI preambles PASS 160/160, imports PASS 187 (+CRC), exports+CRC, binding, modinfo, vermagic, objects (all 56) PASS; 156 of 182 functions byte-identical (26 differ; function-relocs 20 differ), checked against  ... |
| qca_cld3_kiwi_v2 | PARTIAL | qca_cld3_kiwi_v2 - PARTIAL (G54a 22.10.2026: 18405 of 18421 functions byte-identical vs the OS4 GLOBAL stock (code words + resolved relocations) after the documented per-site __LINE__ alignment (17808 without it); the other 16 functions are listed with verdicts below; built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn +  ... |
| qca_cld3_peach | PARTIAL | qca_cld3_peach - PARTIAL (G54a 22.10.2026: 18454 of 18471 functions byte-identical vs the OS4 GLOBAL stock (code words + resolved relocations) after the documented per-site __LINE__ alignment (17573 without it); the other 17 functions are listed with verdicts below; built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn + re ... |
| qca_cld3_peach_v2 | PARTIAL | qca_cld3_peach_v2 - PARTIAL (G54a 22.10.2026: 18585 of 18601 functions byte-identical vs the OS4 GLOBAL stock (code words + resolved relocations) after the documented per-site __LINE__ alignment (17982 without it); the other 16 functions are listed with verdicts below; built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn + ... |
| qca_cld3_qca6750 | PARTIAL | qca_cld3_qca6750 - PARTIAL (G54a 22.10.2026: 15972 of 15988 functions byte-identical vs the OS4 GLOBAL stock (code words + resolved relocations) after the documented per-site __LINE__ alignment (15418 without it); the other 16 functions are listed with verdicts below; built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn +  ... |
| qca_cld3_wcn7750 | PARTIAL | qca_cld3_wcn7750 - PARTIAL (G54a 22.10.2026: 18096 of 18112 functions byte-identical vs the OS4 GLOBAL stock (code words + resolved relocations) after the documented per-site __LINE__ alignment (17502 without it); the other 16 functions are listed with verdicts below; built from the PUBLIC CAF qcacld-3.0 tree + OnePlus 13 cmn +  ... |
| rsmc_driver | PARTIAL | rsmc_driver - PARTIAL (91 of 95 function bodies byte-identical in the real Kleaf build; convertchartobytes, convertlonglongtobytes, rsmc_dtx_msg_proc, rsmc_any_msg_proc differ by codegen only (G57a); function set 95/95, relocations, objects, exports, 80 imports, kCFI 75/75 (G57a), modinfo OK) |
| rss_monitor | PARTIAL | rss_monitor - PARTIAL (31 of 32 functions byte-identical; init_module differs by instruction scheduling only (G57a); function set, relocations, objects, 0 exports, 51 imports+CRC, kCFI 28/28, modinfo all OK in the Bazel compare) |
| sched_entity_freq | PARTIAL | sched_entity_freq - PARTIAL (33 of 36 functions byte-identical; the 3 others differ by codegen only (G57a verdicts below); baseline = CN OS4.0.0.6 stock because the GLOBAL OS4.0.0.7 package does NOT contain this module; function set, relocations, objects, 3 exports + CRC, 50 imports + CRC, kCFI 32/32, modinfo OK in the Bazel com ... |
| sia91xx_dlkm | PARTIAL | sia91xx_dlkm - PARTIAL (80 of 104 functions byte-identical vs the GLOBAL OS4.0.0.7 stock; of the other 24, 1 is FIXED in G53a (sipa_probe) and 23 are EQUIVALENT-PROVEN, 0 OPEN; function set, kCFI 92/92, exports 17, imports 90 + CRC, modinfo PASS; the module-level FAILs objects/anon-data/.altinstructions/depends are layout-only,  ... |
| tfa98xx_dlkm | PARTIAL | tfa98xx_dlkm - PARTIAL (341 of 389 functions byte-identical vs the GLOBAL OS4.0.0.7 stock; of the other 48, 12 were FIXED in G53a (only codegen left) and 35 are EQUIVALENT-PROVEN; 1 DELIBERATE divergence: tfa98xx_dbgfs_dieid_read (the stock is mutex_lock + brk, a broken Xiaomi source; ours is a safe stub); function set, kCFI 375 ... |
| ufs_ffu | PARTIAL | ufs_ffu - PARTIAL, MUST STAY STOCK (5 of 8 functions byte-identical; need_update_k = 82 records compared by CONTENT against the GLOBAL OS4.0.0.7 stock: identical (G57a); the 3 differing functions are codegen only; the 30 firmware blobs are zero placeholders by rule, a build from this tree must NEVER be flashed) |
| xr_qi | PARTIAL | xr_qi - PARTIAL, DO NOT LOAD (G48a 20.10.2026: review-g40f finding fixed: android_vh_binder_set_priority, qos caller -> qos target, now runs the inherit block after the QOS_CORRECT notification like the stock [the stock falls through S+148 -> S+14c]; rebuilt md5 b49b76ec6c8de2980a4a0a8c6625d09d, function-bytes still 19 of 369, t ... |
| xr_qs | PARTIAL | xr_qs - PARTIAL, DO NOT LOAD (G63a 23.10.2026: every differing function now has a raw-listing verdict - set_rtg_grp EQUIVALENT-PROVEN, qievent_callback EQUIVALENT-PROVEN in G61a - the "UNPROVEN"/"stay open" words below are the older state; G40f 20.10.2026: all 5 differing functions re-read from the raw listings: qievent_callback ... |

## How the equivalence checks were done

Every module is built from the source in this repository with the Kleaf/Bazel DDK against a GKI 6.6.144 tree with ThinLTO (the stock modules were built on 6.6.57 headers), and the resulting `.ko` is compared with the stock `.ko` of the reference ROM, never run. The first-generation tool compares function bytes, relocations, exported/imported symbols with CRCs and modinfo; the strict tool `tools/compare_ko_v2.py` also compares the content of every data object, the bug table, kCFI hashes and function binding; `tools/preamble_cmp.py` checks the kCFI hash in front of each function. For differing functions: a register-masked instruction multiset diff (`tools/multiset/`), the symbolic tools `symcmp4` (calls, stores and their guarding conditions) and `symcmp5` (exact path conditions, `tools/review5/`) and, where the tools run out, a hand reading of the stock disassembly against the C source. Independent reviewers (separate sessions that did not write the code) repeated this and read functions line by line; `docs/reviews/` has their reports. `docs/VERIFICATION_METHODS.md` describes each method with its blind spots.

## What is NOT verified

- **Anything on hardware.** Not one module was loaded, booted, flashed or run, not even under `dmesg`.
- **Functions that differ in bytes and are only "read"**: PARTIAL means such functions exist. A diff classified "codegen" is a judgement of a reader or a tool, not a proof. Open for example: the `regulation` function of `mca_strategy_quickchg` (read in 16 sections, six of them independently, not proven as a whole) and the event bodies of `process_event` of the quick-charge and basic-wireless modules, `migt_ioctl`, `migt_hardwork` and seven unread functions of `mi_game`, `set_rtg_grp` of `xr_qs`, the rest of `tfa_dev_set_state`/`tfa_dev_start`/`tfa_reset` in `tfa98xx_dlkm` and `sipa_probe` in `sia91xx_dlkm`, five large functions of `matt`, the Wi-Fi drivers in their entirety (117-421 differing functions per variant; the byte tails of `do_acs`, `lim_fill_pe_session`, `cm_process_join_req` are read by paths only). **`bq27z561`** has 56 differing functions and a data-flash path that was never executed. Every fix of the last round was checked against the stock disassembly, but most of them were not independently re-read. **The independent review of the last round (`docs/reviews/g40f_review.md`) found open differences that are not yet fixed in these sources: the xr_qi binder priority hook for a QoS caller with a QoS target (the stock falls through into the inherit block, ours skips it; QoS behaviour only), `mi_game` `mist_frame_load_history_update` (an inlined helper overwrites a counter that the next cluster iteration reuses), `mi_low_lat_mem` `mi_pre_compact_init` (a pointer is left dangling after a pool allocation failure) and several log, trace and histogram-only differences.**
- **Inter-module behaviour** (the charging chain as a whole), concurrency and races, device tree, firmware and user space.
- **Equivalence of anything that touches a battery**: a module that is byte-identical in code still depends on tables, device-tree values and firmware that are not part of this repository.

## Reconstructed modules (no public source anywhere)

| module | status | compared against | strict check |
|---|---|---|---|
| cameralog | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| cameramsger | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| debug_ext | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | strict-equivalent |
| lb | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_adsp_glink | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_business_battery_comp | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_business_charger_comp | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_business_misc_comp | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_qcom_panel | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_qcom_smem | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_qcom_subpmic_proxy | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_qcom_sysfs | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_strategy_fg_comp | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mi_damon | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | strict compare_ko_v2 (per STATUS.md) |
| mi_perf_memory | EQUIVALENT | HyperOS 3.0.6 only (no OS4 counterpart) | old check only |
| mi_schedule | PARTIAL | HyperOS 3.0.6 only (no OS4 counterpart) | - |
| mibbr | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| millet_oem_cgroup | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| miloadtrace | PARTIAL | HyperOS 3.0.6 only (no OS4 counterpart) | - |
| mist | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| process_monitor | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| qcom_adsp_pd_protocol | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| scene_swappiness | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| sia91xx_tuning | EQUIVALENT | HyperOS 3.0.6 name (OS4: `sia91xx_tuning_dlkm`) | differs in metadata only |
| tas25xx | EQUIVALENT | HyperOS 3.0.6 name (OS4: `tas25xx_dlkm`) | equivalent after crc_patch2.py |
| typec_analog_acc | EQUIVALENT | HyperOS 3.0.6 name (OS4: `typec_analog_acc_dlkm`) | strict-equivalent |

## Ported modules

| module | status | compared against | strict check |
|---|---|---|---|
| aw882xx_dlkm | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| binder_gki | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| bootinfo | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| bq27z561 | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| charger_partition | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| cs35l43_dlkm | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | strict-equivalent |
| dcvs_main | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| dump_binder | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| dynamic_readahead | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| fault_report | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| fs19xx_dlkm | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| gsim | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | strict-equivalent |
| hardwareinfo | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| hl7603 | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| hwid | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| matt | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_basic_wireless | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_bmd | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_buckchg_jeita | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_charge_interface | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_charge_mievent | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| mca_charger_thermal | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_common | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_connector_antiburn | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_event | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_hwid | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_ibat_ocp_monitor | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_log | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_lpd_detect | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_parse_dts | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_path_control | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_pd_auth | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_base | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_bc12_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_buckchg_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_cp_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_fg_ic_ops | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| mca_platform_loadsw_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_platform_wireless_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_protocol_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_protocol_pd_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_protocol_qc_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_pwm_fan | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_quick_wireless | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_smart_charge | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_soc_limit | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_strategy_buckchg | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_strategy_class | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_strategy_fg_class | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| mca_strategy_quickchg | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_sysfs | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_vbat_ovp_monitor | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mca_wireless_revchg | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mca_workqueue | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_fp | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_game | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mi_low_lat_mem | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mi_memory | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in bug-table line numbers only |
| mi_mempool | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_power | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_reclaim | PARTIAL | HyperOS 4.0 CN beta (OS4.0.0.6) | - |
| mi_stack | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_thermal_interface | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in bug-table line numbers only |
| mi_trace | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_ubt | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_ubt_test | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_wmark | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mi_wq_dynamic_priority | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| miev | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| mii | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| miicmpfilter | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| millet_binder | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| millet_core | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| millet_hs | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| millet_pkg | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| millet_sig | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| minet | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | strict compare_ko_v2 (per STATUS.md) |
| minidump | EQUIVALENT | HyperOS 3.0.6 | equivalent after crc_patch2.py |
| miscore | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| mitee_dlkm | PARTIAL | HyperOS 4.0 CN 0.6 and GLOBAL 0.7 (code identical) | - |
| nuvolta_1652 | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| nxp-nci | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | strict-equivalent |
| perf_actuator | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| perfmgr | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| plat_common | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| qca_cld3_kiwi_v2 | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| qca_cld3_peach | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| qca_cld3_peach_v2 | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| qca_cld3_qca6750 | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| qca_cld3_wcn7750 | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| rsmc_driver | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| rss_monitor | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| rtg | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| sc8581_sy6541 | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| sched-penalty | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| sched_entity_freq | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| si_core_module | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| sia91xx_dlkm | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| sia91xx_tuning_dlkm | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | differs in metadata only |
| sla | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_console | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_core | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in bug-table line numbers only |
| stm_ftrace | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_heartbeat | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_nfc_i2c | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_p_ost | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| stm_st54se_gpio | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | strict-equivalent |
| swinfo | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | strict compare_ko_v2 (per STATUS.md) |
| synaptics_tcm2 | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| tango32 | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| tas25xx_dlkm | EQUIVALENT | HyperOS 4.0 GLOBAL (OS4.0.0.7) | equivalent after crc_patch2.py |
| tfa98xx_dlkm | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| typec_analog_acc_dlkm | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | strict-equivalent |
| ufs_ffu | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| vip_sched | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in bug-table line numbers only |
| xiaomi_touch | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| xiaomi_wifi_gpio | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in bug-table line numbers only |
| xm_power | EQUIVALENT | HyperOS 4.0 CN beta (OS4.0.0.6) | differs in metadata only |
| xr_qi | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |
| xr_qs | PARTIAL | HyperOS 4.0 GLOBAL (OS4.0.0.7) | - |

Modules marked as compared against HyperOS 3.0.6 only were not re-checked against the HyperOS 4.0 binaries: for those, the OS4 stock module may differ (in the first OS3/OS4 check of 20 shared modules, 13 were identical, 7 changed, 6 disappeared). Treat them as a starting point.

Modules still blocked: none.

## Building

Each module directory has a `BUILD.bazel` (Kleaf DDK) and, where needed, a `<module>_defconfig` that enables ThinLTO. Labels use the layout we built with, `//re/<name>`: merge `reconstructed/*`, `ported/*` and `stubs/*` into one `re/` directory of a GKI 6.6 Kleaf tree. Exact commands and the lessons learned are in `docs/METHOD.md`. Third-party firmware arrays and tuning data are not published (`ported/nuvolta_1652/inc/FIRMWARE_NOT_INCLUDED.txt`; `ufs_ffu`: the firmware images; `swinfo`: `config_platform_data.h`; see `docs/EXPORT_PRIVACY.md`): to build those modules you have to supply them from your own stock modules.

## HyperOS 4.0 (Android 17) notes

The first sources were written against the HyperOS 3.0.6 binaries. A function-level comparison with the HyperOS 4.0 beta modules (`OS4.0.0.6.XOCCNXM`) of the 20 modules both versions share showed: 13 identical, 7 changed (`mca_qcom_smem`, `mca_qcom_subpmic_proxy`, `mca_business_charger_comp`, `mca_strategy_fg_comp`, `mibbr`, `lb`, `scene_swappiness`), 6 disappeared (`mi_perf_memory`, `mi_schedule`, `miloadtrace`, `typec_analog_acc`, `tas25xx`, `sia91xx_tuning`), 12 are new (`dump_binder`, `dynamic_readahead`, `fault_report`, `matt`, `mca_pwm_fan`, `mi_game`, `mi_low_lat_mem`, `mi_reclaim`, `mi_wq_dynamic_priority`, `miscore`, `perf_actuator`, `plat_common`). Since then the sources were rebuilt and corrected against the OS4 binaries (the "compared against" column). The stock OS4 modules are CRC-compatible with a 6.6.144 kernel (638 of 638 checked), so the stock binary can be used for every module that has no equivalent source yet.

## Kernel package result

Putting the verified modules together with the stock ones for a 6.6.144 kernel gives a 640-module set in which 52 modules are built from our sources, 18 from public kernel sources, 96 are the kernel's own build and 474 are the unchanged stock modules (the package itself is not published: it contains stock binaries). `tools/verify_pkg.py` passes (CRCs, `depends=`, kCFI, load order) and the strict comparison of the 100 non-stock files found 6 strict-equivalent, 86 differing in metadata only, 8 in bug-table line numbers only and 0 real differences. None of the modules of the last rounds is in it (see `docs/KERNEL_PACKAGE.md` for the update of 20.10.2026 against the global stock). The package was never flashed or run. Details are in `docs/KERNEL_PACKAGE.md`.

## License and provenance

The code is licensed **GPL-2.0** (see `LICENSE`). The original modules are GPL-compatible licensed kernel modules shipped by Xiaomi (their modinfo declares `GPL`, `GPL v2` or `Dual BSD/GPL`). Reconstructed sources are derived from those binaries; ported sources derive from Xiaomi's public kernel source releases and keep their original copyright headers. If you are a rights holder and see a problem, please open an issue.

Contributions welcome: hardware test reports, fixes for PARTIAL modules, independent reviews (the code written after the G22 charging review has had a partial independent review: the quick-charge and basic-wireless fixes of 20.10.2026 yes, the last `xr_qi` and `mi_low_lat_mem` fixes yes (`g40f_review.md`), the all-function audits of the charging family and `bq27z561` and the audio drivers no), and sources for the modules still missing (see `docs/source-map/`).
