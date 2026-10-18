# Wi-Fi roaming on Xiaomi 15 (dada): what the driver code says (investigate-wifi-roaming, 18.10.2026)

Read-only. No phone, no builds, no source edits. Source = our reconstruction package `<work-dir>`
(paths below are relative to it unless prefixed), stock = `<work-dir>` (OS4) and the OS4
images extracted in `<work-dir>`. Scratch: `<work-dir>` (cfgdef.py = ini-default extractor, cfg_all.txt = 1113
ini items with file:line/min/max/default, Wi-Fi APEX unpacked, cfg_parse disassemblies), local copies of ini/overlays in the session scratchpad.

## 1. Which driver the phone uses

- All five WLAN modules are shipped and loaded (`<work-dir>`), the chipset selects one;
  the board property says **`ro.hardware.wlan.chip=qca_cld3_peach_v2`** (`os4/root/odm/etc/build.prop:256`, also `ro.hardware.wlan.dbs=2`,
  `mimo=1`, `ro.vendor.net.enable_dual_wifi=1` line 35). The LineageOS tree ships only the peach_v2 ini
  (`work/dada-devel/vendor_xiaomi_sm8750-common/proprietary-files.txt:1568`). So: **qca_cld3_peach_v2** (WLAN.. 5.2.1.119O per STATUS).
- INI: `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini` (symlinked from `/vendor/firmware/wlan/qca_cld/peach_v2/`). The WLAN firmware
  itself (amss/bdwlan) is in a radio partition that is not extracted here, its version was not checked.
- Build flags (g4_peach-v2_defconfig): LFR3 (`CONFIG_QCACLD_WLAN_LFR3=y`, firmware roam offload), 11r/ESE/ADAPTIVE_11R, SAE + SAE_SINGLE_PMK,
  FILS, 11BE + MLO, BAND_6GHZ, DENYLIST_MGR, RX_LINKSPEED_ROAM_TRIGGER, ROAM_INFO_STATS. `CONNECTION_ROAMING_CFG` is **not** set, so the
  `#else` default block of `components/mlme/dispatcher/inc/cfg_mlme_lfr.h:77-126` applies.

## 2. Stock vs our reconstruction (roaming code)

Not the cause of anything on the phone today (the phone runs the stock Xiaomi module), but checked as asked:
- Roam functions in the non-identical list (g4u `logs/cmp_t3h_v.txt` line 2, 133 functions): `wlan_cm_calculate_bss_score`,
  `cm_update_roam_scan_channel_list`, `wlan_cm_update_roam_stats_info`, `wlan_hdd_cfg80211_set_ext_roam_params`,
  `csr_roam_auth_offload_callback`, `cm_fw_roam_invoke_fail`, `wlan_hdd_cfg80211_update_connect_params`, `cm_csr_connect_rsp`,
  `wlan_fill_bss_desc_from_scan_entry`: **all EQUAL modulo `__LINE__`** in the symbolic compare (`<work-dir>`
  lines 27, 161, 162, 404, 571, 1369, 1370, 1345, 1533). Every other `cm_roam_*` / `wlan_cm_roam_*` / `csr_roam_*` function is byte-identical.
  `lim_fill_pe_session` and `cm_process_join_req` (connect/roam session setup) differ only by codegen per g4u STATUS (no behaviour found).
- **Ini defaults**: `cfg_meta_lookup_table` (names/min/max/fallback, 49080 B) identical stock vs `g4u/built/u2h_peach_v2.ko`
  (objx.py: no diff). The defaults are written by `cfg_store_set_defaults()` (`cmn/cfg/src/cfg.c:329`) inlined into `cfg_parse`;
  `cfg_parse` is byte-identical in the g4u build (not in the 133 list; disassembly diff stock vs ours = relocation/branch addresses only,
  e.g. stock `cfg_parse+0x1b58 mov w22,#-0x4c` (-76) at the same instruction index as ours). The one earlier default difference (F4,
  `mi_sap_only_allow_sta_dfs_indoor_chan`, not roaming) is fixed in g4u (`cfg_policy_mgr.h:750` default 1).
- Driver commands: same set as stock (review F11: only the order of `hdd_drv_cmds` differs).
=> Roaming behaviour of our reconstruction = stock, as far as host code and defaults go. Firmware is the same blob either way.

## 3. Roaming parameters: effective values (code default -> Xiaomi ini -> runtime)

Firmware does the roaming (LFR3 "RSO" = roam scan offload). The host only sends parameters (WMI) at connect and on updates:
`cm_roam_start_init`/`RSO start` builds them in `components/umac/mlme/connection_mgr/core/src/wlan_cm_roam_offload.c:3435-3462`
(triggers, MAWC, BSS load, idle, RSSI thresholds 1241-1362, scan period 1375, AP profile + scoring 1890, channel list 2393, scan filter
2538, BTM 3219). Ini = `peach_v2/WCNSS_qcom_cfg.ini` (OS4 line numbers; OS3 = identical roam lines).

| item (ini name) | code default (file:line) | Xiaomi ini | effective |
|---|---|---|---|
| low-RSSI roam trigger `gNeighborLookupThreshold` | -76 (`cfg_mlme_lfr.h:1578`, macro line 95) | -76 (ini:15, again :263) | **-76 dBm** (beacon RSSI) |
| 5 GHz offset `lookup_threshold_5g_offset` | 0 (:332) | - | 0 |
| roam scan timer `gNeighborScanTimerPeriod` | 100 ms (:1524) | 200 (ini:14) | 200 ms |
| scan refresh `gNeighborScanRefreshPeriod` | 20000 ms (:1742) | - | 20 s |
| empty-scan refresh `gEmptyScanRefreshPeriod` | 0 (:1797) | - | 0 = no periodic rescan if no candidate |
| full roam scan period `gFullRoamScanPeriod` | 0 (:1771) | - | 0 = never periodic full scan |
| rescan step `gRoamRescanRssiDiff` | 5 dB (macro :83) | - | 5 |
| RSSI diff to candidate `RoamRssiDiff` / 6 GHz | 5 / 5 (:1372/:1401) | 5 (ini:17) | 5 dB |
| `FastRoamEnabled` / `gRoamScanOffloadEnabled` / `gRoamOffloadEnabled` | 1/1/1 (:1299/:1476/:2614) | 1 | on |
| `roam_triggers` bitmap | 0x7FFFF (:2903) | - | all triggers |
| `roam_data_rssi_threshold_triggers` | 0x3 (:653) | **0** (ini:218) | data-RSSI triggers off, beacon RSSI only |
| `roam_data_rssi_threshold` | -70 (:681) | - | unused (triggers 0) |
| beacon miss `gRoamBmissFirstBcnt/FinalBcnt` | 10 / 20 (:1847/:1872) | - | 10/20 |
| candidate min RSSI after bmiss `candidate_min_rssi_for_beacon_miss` | -75 (`cfg_mlme_roam_scoring.h:391`, macro ~51) | **-83** (ini:162) | -83 |
| candidate min RSSI after deauth | -75 (scoring.h:362) | - | -75 |
| `min_rssi_for_2g_to_5g_roam` | -70 (scoring.h:418) | - | -70 |
| hi-RSSI (2.4->5 upgrade) scan: max count / delta / upper bound / delay | 3 / 23 / -30 / 15000 ms (:2014/:2040/:2091/:2065) | - | on only while on 2.4 GHz (`disable_hi_rssi`, offload.c:1278-1286) |
| `gRoamPrefer5GHz` / `enable_5g_band_pref` / `gSelect5GHzMargin` | 1 / 0 / 0 (:2116/:1049/`cfg_mlme_generic.h:532`) | margin **5** (ini:158) | 5 GHz +5 dB |
| `gRoamIntraBand` | 0 (:2138) | - | inter-band allowed |
| `gAllowDFSChannelRoam` | 1 (:1989) | 1 (ini:19) | DFS passive scan allowed |
| idle roam `enable_idle_roam` | false (:2691) | - | **off** (no roam while idle/screen off unless RSSI/bmiss trigger) |
| BSS-load (CU) roam `enable_bss_load_roam_trigger` | 0 (:2237) | - | off |
| PER roam `gper_roam_enabled` | 3 (:775) | - | on (rx+tx) |
| dense roam `gtraffic_threshold` | 400 kBps (:469) | **50** (ini:166) | 50 |
| MAWC `MAWCEnabled` | 0 (:1322) | - | off |
| min delay between roam scans `min_delay_btw_roam_scans` | 10 s (:2496), applied to triggers 0x10DA (:2541) | - | 10 s |
| roam score delta `roam_score_delta` | 0 % (`cfg_mlme_roam_scoring.h` #else block, macro ~41) | - | 0 |
| `RoamCommon_MinRoamDelta` (CU / low-RSSI) | 15 % (scoring.h:226) | - | 15 |
| per-trigger delta `roam_trigger_score_delta` | "10,0,14,0" (scoring.h:546) | **"9,10"** (ini:275) | FORCED/host-invoked +10 %, BTM/IDLE fall back to roam_score_delta (0) |
| scoring `enable_scoring_for_roam` | 1 (scoring.h:318) | - | firmware score-based selection |
| BTM (11v) `btm_offload_config` / `prefer_btm_query` / `minimum_btm_candidate_score` | 0x1C1 / 1 / 2600 (`cfg_mlme_btm.h:122/43/307`) | - | on |
| 11k `gRrmEnable`, NR offload `11k_offload_enable_bitmask` | -, 1 (`cfg_neighbor_roam.h:38`) | gRrmEnable=1 (ini:27); supplicant `driver_param="no_rrm=1"` | driver/firmware 11k |
| 11r `FastTransitionEnabled` / `enable_ftopen` / adaptive 11r | 1 / 1 / false (:1346/:2566/:3105) | **enable_ftopen=0** (ini:170) | FT on, FT-open off |
| SAE single PMK | true (:3069) | - | on |
| denylist `avoid_list_expiry_time` / `black_list_expiry_time` / `bad_bssid_counter_thresh` | 5 min / 10 min / 3 (`components/denylist_mgr/dispatcher/inc/cfg_dlm.h:47/100/73`) | **3 / 5 / 7** (ini:184-187) | shorter, more lenient |
| concurrency `gEnableFastRoamInConcurrency` / `sta_disable_roam` / `enable_dual_sta_roam_offload` | 1 / 0 / 1 (:358/:2934/:2964) | - | roam allowed with P2P/NAN; dual-STA roam on |
| NUD `gEnableNUDTracking` | 0 (`components/dp/dispatcher/inc/wlan_dp_cfg.h:1129`) | **3** (ini:148, overrides =1 at ini:63) | gateway-unreachable -> roam, disconnect if roam fails |
| Xiaomi `gEnableNUDFailureNotify` | 0 (wlan_dp_cfg.h:1136) | OS4: commented (ini:312); **OS3 (dada-devel copy): =4** | see 4c |
| Xiaomi `gEnableBmissNoity` | 0 (wlan_dp_cfg.h:1143) | 1 (ini:315) | bmiss notified to framework |
| WLM latency `wlm_latency_flags_ultralow` / `_xr` | 0xc83 / 0x3000100000083 (`cfg_mlme_fe_wlm.h:383/256`) | **0x180003** / 0x200080083 (ini:130/112) | ultralow: roaming allowed (Qualcomm default allowed only on final bmiss, bits 6-7, fe_wlm.h:161-165); XR: roam only on final bmiss |

Runtime overrides (no ini needed): private `DRIVER` commands `SETROAMTRIGGER/GETROAMTRIGGER` (-100..-50, `core/hdd/src/wlan_hdd_ioctl.c:2883`,
table 7442-7486: SETROAMSCANPERIOD, SETROAMSCANREFRESHPERIOD, SETROAMMODE, SETROAMDELTA, SETROAMINTRABAND, SETSCANHOMEAWAYTIME,
SETOPPORTUNISTICRSSIDIFF, SETROAMRESCANRSSIDIFF, SETFASTROAM, FASTREASSOC, REASSOC ...); vendor nl80211:
`QCA_NL80211_VENDOR_SUBCMD_ROAM` (`wlan_hdd_cfg80211.c:23403`, allow/deny lists, ROAM_CONTROL_SET/GET), `..._ROAMING` = roam policy
(23670 -> 20366: NOT_ALLOWED / ALLOWED_WITHIN_ESS / AGGRESSIVE, this is what the Wi-Fi HAL `setRoamingState` uses), `..._STA_CONNECT_ROAM_POLICY`
(23571), `..._ROAM_EVENTS` (23756). `alert_rssi_threshold` from roam control overrides the ini trigger (offload.c:1259-1262).
Module parameters (`/sys/module/qca_cld3_peach_v2/parameters/`) have no roam knobs (con_mode, fwpath, country_code ..., `wlan_hdd_main.c:23754-23775`).

## 4. Places in the code where roaming is suppressed or ends in a disconnect

a) **RSO off by state/bitmap**: `cm_roam_switch_to_rso_enable` (`wlan_cm_roam_offload.c:4589-4715`): no RSO if `roam_scan_offload_enabled`=0
   ("RSO disabled via INI"), if any internal control bit is set ("RSO Disabled internally ... bitmap"), or if the **supplicant/HAL disabled
   roaming** (QCA_ROAMING_NOT_ALLOWED; state stays RSO_STOPPED until re-enabled, 4641-4647, 4703-4714).
b) **Concurrency**: during connect on another STA (`components/cmn_services/interface_mgr/src/wlan_if_mgr_sta.c:120-138`), during SAP start /
   SAP channel change (`wlan_if_mgr_sap.c:84`, `wlan_hdd_hostapd.c:3475/4061/4957`), NDP (`wlan_hdd_main.c:1808`), channel switch
   (`lim_utils.c:3804`); with P2P client and `p2p_disable_roam` roaming is not re-enabled (`wlan_if_mgr_sta.c:158-166`). STA+STA: only
   one STA roams unless dual-STA roam is active on different MACs (offload.c:4140-4190, 4440-4466); a band change is refused when the two
   STAs sit on different MACs and the roam band mask excludes the band (offload.c:688-735).
c) **Roam-invoke failure = disconnect**: a reassociation from userspace (cfg80211 connect with `prev_bssid` and the same SSID) becomes a
   host roam `CM_ROAMING_HOST` (`cmn/umac/mlme/connection_mgr/core/src/wlan_cm_roam_util.c:95-125`), sent to firmware as WMI roam invoke
   with a fixed BSSID (`components/wmi/src/wmi_unified_roam_tlv.c:1284-1318`). If firmware aborts it, `cm_disconnect_roam_invoke_fail`
   **disconnects** with REASON_USER_TRIGGERED_ROAM_FAILURE (`components/umac/mlme/connection_mgr/core/src/wlan_cm_roam_fw_sync.c:1606-1617`).
   The Xiaomi branch (1594-1604) only avoids the deauth for the NUD-triggered case and only when `gEnableNUDFailureNotify=4`
   (active in the OS3 ini, commented out in the OS4 ini). NUD failure itself adds the AP to the avoid list and invokes a roam
   (`core/hdd/src/wlan_hdd_nud_tracking.c:36-66`). The denylist also aborts a roam invoke to a rejected BSSID (offload.c:6270).
d) Latency levels: XR level allows roaming only on final beacon miss (bits 6-7 of the WLM flags, see table).

## 5. Framework / ROM side (overlays), stock vs LineageOS tree

- AOSP defaults (OS4 Wi-Fi APEX `ServiceWifiResources.apk`, decoded): `config_wifiEnableLinkedNetworkRoaming=false`,
  `config_wifiUseHalApiToDisableFwRoaming=false`, `config_wifiMultiStaMultiInternetConcurrencyEnabled=false`, `config_wifi6ghzSupport=false`.
  `config_wifi_framework_current_network_boost` / `config_wifiFrameworkMinRssi*` no longer exist in this resource set.
- Stock HyperOS overlays: `vendor/overlay/WifiResTarget.apk`: UseHalApiToDisableFwRoaming=**true**, MultiInternet=false, MBB=false;
  `product/overlay/WifiResCommon_Sys.apk`: 6ghzSupport=true, SaeH2e, ... (no roam thresholds). Xiaomi's own Wi-Fi APEX adds a
  "WhiteListRoam" (linked-SSID roaming behind the same `config_wifiEnableLinkedNetworkRoaming`, `ClientModeImpl.updateLinkedNetworks`).
- LineageOS tree overlay (`work/dada-devel/device_xiaomi_sm8750-common/overlay/WifiOverlayCommon/res/values/config.xml`):
  LinkedNetworkRoaming=false, **MultiInternet=true** (stock false), no UseHalApi... (=false), **no config_wifi6ghzSupport** (stock true).
- Framework behaviour (decompiled `service-wifi.jar` of OS4): `WifiConnectivityManager` line ~1545: when its network selection picks
  another BSSID of the **same** network it calls `startRoamToNetwork(bssid)` -> supplicant reassociate -> driver path 4c. With
  UseHalApi=true, `ClientModeImpl.connectToNetwork` (line ~4012) disables firmware roaming whenever it connects to a specific BSSID
  and re-enables it only for "any". Partial connected-mode scans are skipped when firmware roaming is supported (WCM ~1788).
- `wpa_supplicant_overlay.conf`: `driver_param="no_rrm=1"` (same in OS4 and dada-devel).

## 6. Hypotheses, ranked (none proven; the code alone cannot tell which applies)

1. **Sticky by design (firmware triggers)**: roam scan starts only below -76 dBm beacon RSSI (data-RSSI triggers disabled by Xiaomi),
   idle roam and CU roam off, no periodic/empty-scan rescans, candidate must be 5 dB and >=15 % score better, 10 s between roam scans.
   In homes with strong overlapping APs (mesh) the phone keeps a weaker AP until -76 dBm or beacon loss. Same on stock HyperOS
   (identical ini/defaults); HyperOS may add runtime tuning through its framework (not found/verified).
2. **Network layout**: different SSIDs per band or per AP are separate networks for the firmware (linked-network roaming is off in AOSP,
   stock and LOS) -> only disconnect/reconnect; mixed security between APs (WPA2 vs WPA3/SAE, transition mode, PMF, 11r on some nodes)
   makes candidates unusable -> slow handoff or reconnect.
3. **Framework-initiated roam that fails -> disconnect**: the AOSP framework roams to a specific BSSID; firmware may refuse
   (Xiaomi `roam_trigger_score_delta=9,10` asks +10 % score for forced roams - whether the firmware applies it to a fixed-BSSID invoke is
   firmware-internal), and the driver then disconnects (4c). Symptom: short Wi-Fi drop exactly when moving.
4. **Concurrency suppressing roaming**: hotspot, Wi-Fi Direct/cast/Mi Share, Wi-Fi Aware, a second STA (LOS overlay enables
   multi-internet, stock does not), XR latency mode.
5. **ROM/vendor mismatch**: modules from HyperOS 3.0.301 (dada-devel) with firmware/vendor from another build (3.0.6 / OS4), or an
   ini/overlay set that differs from what HyperOS ships (UseHalApi true vs false, NUDFailureNotify 4 vs off): not shown to break roaming,
   but untested.
6. Our reconstructed module: not involved (not on the phone; roaming code and defaults equal to stock).

## 7. What to collect / test

- ROM name+build, `getprop ro.build.fingerprint ro.vendor.build.fingerprint ro.hardware.wlan.chip`, kernel (`uname -a`), whether the
  stock vendor/firmware partitions came from HyperOS 3.0.x or OS4; AP setup: brand/mesh, SSIDs per band, security, 802.11r/k/v, 6 GHz, MLO.
- Symptom class: sticks to far AP / drops then reconnects / never moves to 5 GHz / slow handoff (calls, video).
- `adb logcat -b all -v time | grep -i -E 'roam|wlan_cm|cm_roam|WifiNetworkSelector|WifiConnectivityManager|ClientModeImpl|CTRL-EVENT|BSSID'`
  while walking between APs; `adb shell dumpsys wifi > dumpsys_wifi.txt` right after (WifiConnectivityManager local log shows
  "Roam to"/"Connect to", the firmware roaming capability and blocklists).
- RSSI/BSSID trace once per second: `adb shell "while true; do date +%T; iw dev wlan0 link | grep -E 'Connected|signal|freq'; sleep 1; done"`.
- Root only: `adb shell su -c 'dmesg -w'` (look for `ROAM`, `RSO`, `roam invoke`, `REASON_USER_TRIGGERED_ROAM_FAILURE`, `NUD`, `bmiss`);
  current driver values via `wpa_cli -i wlan0 DRIVER GETROAMTRIGGER` / `GETROAMSCANPERIOD` / `GETROAMDELTA` / `GETROAMMODE`
  (socket path of the ROM's supplicant); firmware logs via the vendor `cnss_diag` if present.

## 8. Change proposals (need a test, not fixes)

- Runtime A/B test first (no reflash, lost on reconnect/reboot): `wpa_cli ... DRIVER SETROAMTRIGGER -70` (and optionally
  `SETROAMDELTA`), walk the same path, compare traces.
- If it helps, an ini override (Magisk/KernelSU overlay of `/vendor/etc/wifi/peach_v2/WCNSS_qcom_cfg.ini`, or the LOS blob copy):
  `gNeighborLookupThreshold=-70..-72`, `roam_data_rssi_threshold_triggers=3` (Qualcomm default), `enable_idle_roam=1`,
  `roam_trigger_score_delta=10,0,14,0` (Qualcomm default, drops the +10 % for host-invoked roams), optionally `gEmptyScanRefreshPeriod`
  > 0. Trade-off: more roam scans = more power and more ping-pong; the -76 value is Xiaomi's/Qualcomm's choice, not a bug.
- ROM overlay: consider `config_wifi6ghzSupport=true` (stock) and `config_wifiMultiStaMultiInternetConcurrencyEnabled=false` (stock)
  if concurrency is suspected. No driver code change is indicated by the code review; the reconstructed defaults must stay equal to stock.

Not determinable from code: firmware candidate scoring/thresholds actually applied, firmware behaviour on fixed-BSSID roam invoke,
the real ROM/vendor combination on the phone, and the AP side.
