# Wi-Fi: the five `qca_cld3_*` drivers

State of 19.10.2026 (night); the Wi-Fi driver work itself stopped with round 11 (18.10.2026). **Nothing here has run on a device.** All five drivers are PARTIAL. This page says what is published, how the drivers were rebuilt, what an independent review found, and where the work stopped.

## What the five modules are

The stock Xiaomi 15 (`dada`) carries five Qualcomm `qcacld-3.0` host drivers in HyperOS 4.0: `qca_cld3_peach`, `qca_cld3_peach_v2`, `qca_cld3_kiwi_v2`, `qca_cld3_qca6750` and `qca_cld3_wcn7750`. The driver tree is the public Qualcomm/OnePlus one (CAF `qcacld-3.0`); it is **not** republished here. What is published is only what is ours:

- `ported/qca_cld3_<variant>/STATUS.md`: per-variant status and numbers;
- `ported/qca_cld3_xiaomi_ext/`: the Xiaomi extension code we had to write (`xiaomi_ext.c/.h`, the P2P header compression `p2phc_core.c`, `p2phc.h`, `dlhc.c`, small patch snippets and `tagpatches/`, the `#line` pin generator `linehack.py` with its table, `apply_xiaomi_ext.py` that applies all of it to the public tree) and `xm_compat_6657.h` (see below);
- `ported/qca_cld3_recipe/`: the helper scripts of the shared recipe (assemble the driver tree for a variant, generate the Bazel file, name and function listings). The ssh wrapper of the build server is not included;
- `docs/wifi/g4q`, `g4r`, `g4s`, `g4t`, `g4u`: the notes of the rounds 7 to 11 (`NOTES.md`), the numbered behaviour specs (`specs/32` to `specs/89`, one small file per recovered behaviour), the patches of the last two rounds and the strict-comparison output of one round. The specs are descriptions of what the stock binary does and where our code now matches it.

No stock binary and no Qualcomm firmware is in this repository.

## Method

The five variants share one source with per-variant defines. For each round the shared source was changed, all five were rebuilt with the real Kleaf/ThinLTO build, and the result was compared with the HyperOS 4.0 stock module with `tools/compare_ko_v2.py` and `tools/preamble_cmp.py`. Behaviour differences were found by reading the stock disassembly next to the C (call and string sequences, symbolic facts with `__LINE__` masked, stack slots), fixed in the shared code and described in a spec.

## Numbers (round 11, last)

| variant | functions | byte-identical without / with the `#line` pin table |
|---|---|---|
| `peach_v2` | 18446 | 17825 / 18313 |
| `peach` | 18324 | 17424 / 17903 |
| `kiwi_v2` | 18271 | 17656 / 18137 |
| `qca6750` | 15880 | 15309 / 15763 |
| `wcn7750` | 17981 | 17368 / 17852 |

No function exists only in the stock or only in ours in any variant. kCFI preambles: 17008 of 17008 equal for `peach_v2`, 0 mismatches in `preamble_cmp` for all five. Exports, imports (584 with CRC for `peach_v2`), modinfo, binding and the function set PASS. The strict comparison still FAILS on function bytes (133 functions of `peach_v2`; more in the other variants), function relocations (159), eight zero-size objects, anonymous data, bug-table lines and jump-table contents (the alternatives count is equal). None of the variants is strict-equivalent.

## What the independent review found

`docs/reviews/wifi_review1.md` (an independent review by a separate Opus-level model, reading all 144 differing functions of `peach_v2` symbolically and dumping the 21 objects that had been filed as "relocation layout") found that the strict tool had been hiding real data differences. Findings F1 to F7 (all five variants unless noted), and what happened to them in the following round:

| id | severity | finding | result |
|---|---|---|---|
| F1 | high | the P2P header-compression netfilter hook (`p2phc_hooks`) was registered as IPv4 PRE_ROUTING; the stock registers it as INET at the ingress hook of the P2P device. Compressed packets of a Xiaomi peer would have been dropped by the IP layer before reaching the hook, stalling the connection | fixed (checked against the stock bytes) |
| F2 | medium | 90 words of five policy-manager PCL tables differ: the stock prefers 5 GHz SCC in concurrent mode, ours picked 2.4 GHz | fixed (overrides appended by a script, not the original table text) |
| F3 | medium | interface combination STA+P2P+P2P was smaller than the stock (2 P2P and 3 interfaces against 3 and 4) | fixed |
| F4 | medium | default of `mi_sap_only_allow_sta_dfs_indoor_chan` was 0, the stock default is 1 | fixed |
| F5 | low-medium | the Miracast optimisation timer was 5 s: the immediate in the stock is in jiffies (20 s), it had been read as milliseconds | fixed |
| F6 | low-medium | an extra atomic test in the SAP/STA DFS channel-switch failure path that the stock does not have | removed |
| F7 | low | one setter entry short in `interdependent_setters` (20 against 19), plus (F8) a missing 76th vendor event, (F9) flags of vendor command 251 on `peach`/`peach_v2` | fixed |

F10 and F11 (a newer-CAF DBR deinit path that is safer than the stock; a statistics bit, message order and log level, cosmetic table rows) were taken over in the same round. The review also confirmed that the deliberate "bug-compatible" changes of the earlier rounds are correct or harmless, and listed stock bugs that were kept on purpose (see `docs/STATUS_AND_LIMITS.md` section 7).

Other real differences found by our own rounds 9 to 11 (about 22 in round 9 alone, specs 62 to 89): the RSNXE and MSCS information elements were dropped from the association request by the public code (a real functional bug of the public tree relative to the stock), wrong SAP/STA channel-combination rules in the policy manager, several timeouts and sizes, the APF instruction size, a `kzalloc` in the header compression init, and one log string that differed while all function bytes were equal (found only by scanning relocated string text).

## Where the work stopped

After round 11 the remaining twelve `peach_v2` functions that still differ were read against the stock one by one: the differences are inlining, tail merging, stack-slot placement and duplicated guards, with the same checks and the same arithmetic. All 2828 named objects are equal in content except eight zero-size arrays that are only placed in another section position. Of the 155 functions with differing bug-table lines, 151 differ because the stock was built with 6.6.57 headers (section "Kernel drift" below); three need the driver sources to be split into files exactly like the stock (`p2phc_switch.c`, `dlhc.c` line numbers, `qdf_trace.c`), which was not done. **The functional differences have run out at the level of binary checking.** That means: the binary comparison, the symbolic comparison and the review no longer find a behaviour difference. It does not mean the driver works: no code was run, the review could miss what none of these methods can see, and several places are bug-compatible with the stock on purpose.

Round 11 in detail: the last twelve differing `peach_v2` functions (`lim_fill_pe_session`, `policy_mgr_allow_4th_new_freq`, `cm_process_join_req`, `target_if_direct_buf_rx_module_register`, `wlan_hdd_update_wiphy`, `do_acs`, ...) were read next to the stock and show only codegen differences; the data comparison found the 2828 named objects equal in content apart from eight placement-only arrays; a scan of the relocated string text found one real difference (a log message of the MLO pairwise-key path) that no byte comparison could see, and it was fixed. A separate search for globals that the stock reads where ours has a constant (`docs/reviews/multiset_audit2.md`, part 3) found the multiset of global reads identical in all 18451 functions of `peach_v2`.

## Roaming investigation (a user report, read-only)

A report that roaming does not work well on a phone running this hardware prompted a read-only investigation of the code and the HyperOS 4.0 images (`docs/reviews/wifi_roaming_investigation.md`; no build was run, no source changed, no phone was used). Results:

- **No roaming bug in the driver code.** The phone uses `qca_cld3_peach_v2` (selected by the board property `ro.hardware.wlan.chip`). Roaming is done by the Wi-Fi firmware (LFR3 roam scan offload); the host only sends it thresholds over WMI. All roaming functions of our reconstruction are equal to the stock up to line numbers, the ini default table and `cfg_parse` are byte-identical, and the phone runs the stock module anyway, so the reconstruction is not involved.
- **The thresholds are set by the Xiaomi ini** (`/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini`), and they make the phone hold a far access point for long: roam scanning starts only below -76 dBm beacon RSSI (`gNeighborLookupThreshold`); the data-RSSI roam triggers are switched off (`roam_data_rssi_threshold_triggers=0`); idle roaming and channel-load roaming are off; there is no periodic or empty-scan rescan; a candidate must be 5 dB stronger and at least 15 percent better in score; at least 10 s pass between roam scans. Other Xiaomi values: roam scan timer 200 ms, candidate floor after a beacon miss -83 dBm, 5 GHz preference margin 5 dB, `roam_trigger_score_delta="9,10"` (+10 percent for host-invoked roams), `enable_ftopen=0`, shorter deny-list times (3 / 5 min, 7 bad-BSSID hits), NUD tracking 3, latency-level flags for the ultra-low and XR modes.
- **Hypotheses, none proven** (the code alone cannot tell which applies): (1) the behaviour is sticky by design (strongest in mesh networks with strongly overlapping access points, and the same on stock HyperOS because the ini and defaults are identical); (2) the network layout: different SSIDs per band are separate networks for the firmware (linked-network roaming is off in AOSP, the stock ROM and LineageOS), and mixed security between access points (WPA2 and WPA3, 802.11r on some nodes only) makes candidates unusable; (3) a framework-initiated roam to a specific BSSID that the firmware refuses ends in a driver disconnect with `REASON_USER_TRIGGERED_ROAM_FAILURE` (a short drop exactly while moving); (4) concurrency (hotspot, Wi-Fi Direct, casting, Wi-Fi Aware, a second STA connection, XR mode) silences roaming; (5) a ROM/vendor mismatch (modules, firmware and ini from different builds); (6) the reconstructed module is not involved.
- **What it proposes, as tests and not fixes:** a runtime A/B test with `DRIVER SETROAMTRIGGER` through the supplicant, then an ini override (`gNeighborLookupThreshold` -70 to -72, the Qualcomm default of `roam_data_rssi_threshold_triggers`, `enable_idle_roam=1`), accepting more power use and ping-pong; and the logs to collect while walking between access points. What cannot be determined from code: the thresholds the firmware actually applies, its behaviour on a fixed-BSSID roam invoke, the real ROM/vendor combination and the access-point side. The Wi-Fi firmware itself is not published here (no firmware file lists or blobs); the report only quotes ini parameter names and values.

## Hacks and bug-compatible places

- **`#line` pin table** (`xiaomi_ext/linehack.py`, `linehack_peach_v2.json`): pins `__LINE__` of log macros and bug-table entries to the stock numbers. Not restored code, only equalises numbers. The non-hack build is the honest one: 17825 of 18446 for `peach_v2`.
- **`xm_compat_6657.h`**: the stock was built against kernel headers 6.6.57, ours against 6.6.144. The header is force-included (`copts`) and restores the 6.6.57 bodies of `netif_tx_stop_queue` (no `trans_start` write and barrier) and `readl`/`writel` (unconditional `log_*_mmio` calls). **It is a build-environment emulation of a header difference, not Xiaomi code.** Effect of the 6.6.57 body: a queue stopped through the driver's inlined stop path after a long idle period can raise a false tx-timeout (log and counters), exactly as on the stock kernel. The header also silences `-Wmacro-redefined` for every translation unit.
- **Bug-compatible places**: a dead flag at `pe_session +0x300a` (three readers, no writer; the HT40 IoT block is inert as in the stock), an HT-only channel-width cap, no forced SCC in the 4th-connection rule. These are on purpose, and the first of them was misread once (a note of a round claimed that `lim_sta_send_add_bss` read the flag; a later round showed it does not).
- **Per-variant switches**: `CONFIG_CNSS_HW_SECURE_DISABLE` is removed for `qca6750`/`wcn7750` (the stock has no soft unload there); `XM_P2P_MODE_NEED_RUNNING` for `peach`/`peach_v2`.
- Names of unnamed Xiaomi structures and functions (`MI_*`) in `xiaomi_ext` are guessed from use.

## Stock bugs kept on purpose

See `docs/STATUS_AND_LIMITS.md` section 7: remote out-of-bounds read in the header-compression parsers, an ignored enable sysctl, and a panic reachable from a vendor ACS command. They exist in the stock binary and are reproduced so that the behaviour matches. After the F1 fix the P2P parsers are reachable again, as in the stock.

## Kernel drift

151 of the 155 functions with different bug-table line numbers are caused only by header line numbers of 6.6.57 versus 6.6.144 (`cfg80211.h`, `skbuff.h`, `netlink.h`, `if_vlan.h`). Nothing can be done about it in the driver source short of building against a 6.6.57 tree.
