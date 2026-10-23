# Independent review: bq27z561 `bq_fg_probe` gauge / HW-write steps and the G17b probe + helper code (GLOBAL OS4.0.0.7)

Reviewer: review agent (read-only; wrote none of this code). Date 20.10.2026. Nothing under `work/re/src` edited; no phone, no adb/fastboot/git; nothing built. Community-only module. No safety claim anywhere below.
Inputs: stock `<work-dir>` (md5 6277f2d7...); our G17b build `<work-dir>` (md5 f301f745...); source `work/re/src/bq27z561/bq27z561.c` md5 926f2f84..., `inc/bq27z561.h` md5 bc371a0d... (that this source produced exactly that .ko was NOT re-checked: no rebuild, server load ~20). DT: dtbo `os4g/an/dtbo/g.dts` (single overlay entry, identical to th.dts) and the 8 vendor_boot DTs `os4g/an/dtb_gl/0*.dts`. `CONFIG_HZ=250` (ikconfig_global.txt).
Method: `llvm-objdump -dr` (clang-r510928) of both modules, `.rodata` strings and jump tables decoded from the raw sections (own `ann.py`, `calls.py`), struct offsets from the G14g BTF dump (`struct bq_fg_chip`, 1856 B), G17b `compare_ko_v2` report, fg_comp / platform listings of `<work-dir>`, `llvm-nm -u` over all vendor_dlkm modules. Scratch `<work-dir>` (4.4 MB). symcmp4 not run (load).
Labels: VERIFIED = read in the listing / DT / report by me; INFERRED = deduced, not proven. Key values (DT unseal_key) and firmware tables are deliberately not reproduced.

## 1. The five steps (stock facts, gates, dada reachability, recommendation)

### S1 `gpiod_direction_output_raw(gpio_to_desc(ctl_co_gpio), 0)` (stock .text+0xaf00..0xb00c, inlined bq_parse_dt)
- VERIFIED: `co_by_gpio = of_find_property(node, "co_by_gpio") != NULL` (int, chip+0x6a4). If set: `ctl_co_gpio = of_get_named_gpio(node, "batt_ctl", 0)` (+0x6a8); `< 0` -> err log (line 7592) and the REST of bq_parse_dt is skipped (base_termv / base_termv_gl / delta_soh_compesation[_gl] not parsed). Else `devm_gpio_request(dev, gpio, dev_name(dev))`; failure -> err log (7598), no direction change; success -> direction output, raw level 0; failure -> err log (7602). No retry, no return code propagated. Always afterwards `gpiod_get_raw_value` + info log (7606).
- What it changes: a SoC GPIO pin (direction + level 0), not gauge memory; volatile (SoC pin state, reset on reboot). The same pin is later driven by `fg_set_co_by_gpio` (fg_set_one_co / fg_toggle_co) when co_by_gpio is set. Meaning of level 0 on the pack side: INFERRED only ("CO" = charge-path control).
- Gate: DT `co_by_gpio` (+ `batt_ctl`). dada GLOBAL: absent in dtbo and in all 8 vendor_boot DTs (VERIFIED, grep count 0; also `batt_ctl`, `base_termv`, `delta_soh`, `support-dynamic-fv`, `support_nvt1000_ota` = 0). Dead on dada.
- Ours: identical sequence, only the direction call is left out (VERIFIED: probe calls devm_gpio_request, 1x gpio_to_desc, gpiod_get_raw_value; no gpiod_direction_output_raw).
- Recommendation: keep stubbed (no observable difference on dada). Port possible (one reversible call) only for a target whose DT sets co_by_gpio, together with a check of the fg_set_co_by_gpio semantics.

### S2 NFG1000 boot-app judge (stock .text+0xb20c..0xb630)
- VERIFIED gate: `support_nvt1000_ota` = `mca_parse_dts_u32(node, "support_nvt1000_ota", ..., 0)` (u32, +0xdc). It also changes the device-presence check: nvt != 0 -> `__fg_read_word(reg 0x0A)` under trylock; nvt == 0 -> MAC 0x4C, 32 B. Ours always uses the MAC 0x4C read (difference, dead on dada).
- VERIFIED sequence (only nvt != 0, after presence check): up to 3x `__fg_read_word(Voltage reg 0x08)` with BLOCKING `mutex_lock` (no ota check), msleep(100) after each failure; then up to 3 rounds: raw `__fg_write_block(client, 0x3E /*ALT_MAC*/, {0x00,0x02}, 2)` (return value ignored, no ota gate), `usleep_range(1000,1100)`, `__fg_read_byte(0x3F)` ("boot_cmd"), msleep(100) on read failure. The "3 writes" are retries of a failed READ; normally exactly 1 write. Decision: Voltage == 0 && boot byte == 1 (gauge in its bootloader) -> `nfg1000_update_APP` (full gauge firmware programming: EnterBootLoad, SHA auth, chip-id, erase / program / CRC, ExitBoot; uses the stock-only firmware tables) up to 3 attempts, each followed by `nfg1000_ota_program_step7_ExitBoot` (raw writes to reg 0x00 and 0x3D, read 0x3F expecting a 3-byte ack, 5 tries, 2 ms + 10 ms waits) when update_erase_flag (+0x490) != 0xAAAAAAAA, and msleep(1500). Any other outcome -> `nfg1000_update_judge` (read-only: MAC 0x4A name, 0x4B chem, 0x05 / 0x08) and, if it returns 0, chip+0x48C = 0xAA5555AA = the ONLY setter of the flag that arms `nfg1000_ota_update_check` (ops slot 65; VERIFIED: only store of that value at .text+0xb630, cleared at .text+0x19db4).
- What it changes: the {0x00,0x02} write = a 2-byte MAC subcommand (status query; volatile: INFERRED); update_APP = persistent gauge firmware flash (not reversible by reboot).
- dada: `support_nvt1000_ota` absent (VERIFIED) -> whole branch dead; the fg_comp side (`strategy_nvt1000_ota_monitor_work`) is gated by fg_comp's own `support_nvt1000_ota` key, also absent (VERIFIED string + DT grep).
- Recommendation: drop for dada (never port the firmware path). If a target DT sets the key, port first only the presence-check variant (read word 0x0A) and keep the rest out; that target would otherwise behave differently from the stock.

### S3 `fg_set_force_sim_soh` for device name "3XM31" (stock .text+0xc540..0xc7bc, function .text+0x10928)
- VERIFIED condition: hwid != NULL && platform_version == 3 && country_version == 0 (stock logs "not miro CN device" otherwise). Then chip+0x17F cleared (8 B), MAC 0x4A read 8 B (trylock form), 5 bytes copied, `strcmp(.., "3XM31")`; equal -> fg_set_force_sim_soh; else log "not CN NVT device"; read error -> log, probe continues.
- VERIFIED fg_set_force_sim_soh: `fg_mac_write_block(bq, 0x1056, buf, 0)` = MAC command with NO data (bus: 0x3E <- 2 command bytes, 0x60 <- checksum + length 4); no secure delay (cmd < 0x4000), no unseal; msleep(30); read MAC 0x2001 (2 B), done when byte0 bit 2 is set; otherwise the command is sent again: max 3 sends; errors only logged; void.
- Persistence: NOT established. Against "data flash" (STATUS wording): command < 0x4000, length 0, no unseal (every stock DF writer unseals first) -> looks like a MAC mode command (INFERRED). If it is a toggle, a re-send after a slow status read could flip it back (INFERRED).
- dada: platform_version 2 per the review-6/7 hwid table (INFERRED: hwid comes from a bootloader module parameter, not visible statically; consistent with the term-voltage jump table pv1/pv2 -> "G1" O2/O3 and the dada DT model "O3_5400mah_90w"). Dead on dada.
- Ours: same reads / compare / logs, call omitted (VERIFIED). Extra log difference: after a failed name read ours also prints "FuelGaugw probe successfully"; the stock jumps past it (cosmetic, dead on dada).
- Recommendation: keep stubbed / drop for dada.

### S4 `queue_delayed_work_on(WORK_CPU_UNBOUND, system_wq, term_voltage_update_work, 2500)` (stock .text+0xc674; BQ7-5)
- VERIFIED: INIT_DELAYED_WORK at .text+0xb168; queued on EVERY probe end: success, failed device-name read, and the "Over probe cnt max" path (gauge not found 50x, probe returns 0 without platform_fg_ic_ops_register). 2500 jiffies = 10 s at HZ 250.
- VERIFIED work (.text+0xc854, 4040 insns): `mca_get_hwid_info`, jump table .rodata+0x22c by platform_version: 1,2 -> "G1", 4 -> "G2", 7 -> "O10U", 11 -> "P11", others nothing. G1: MAC 0x4B 4 B, version byte3 < 0x45 else "no need"; MAC 0x4C header 'MI' else exit; byte3 >= '3' -> MAC 0x4A 36 B, project O2S / O2 / O3 parsed from the name (stored at chip+0x738); requires 0x4C byte2 == '6'; `fg_set_seal(1)` (unseal with the DT key); OperationStatus 0x54 check; then read-modify-write of data-flash addresses 0x4417 / 0x4414 / 0x448B (full current, normal / FFC full voltage, "old params" variant) via fg_mac_write_block (cmd > 0x4000 -> extra msleep(100) inside), each up to 3 writes with msleep(200) and read-back verify, raw block + checksum writes to 0x3E / 0x60 for the FFC voltage, version write 0x440C, `fg_set_seal(0)`. Persistent pack data flash (INFERRED from the 0x4xxx range + unseal; nothing restores old values).
- dada: reachable in the stock (pv 1/2 -> G1 path); whether it writes depends on the pack (version byte < 0x45, 'MI' info, '6', O2/O3 name) = unknown without a stock log of the device ("0x004B = ..." line). Ours: stub never queued (VERIFIED: no queue_delayed_work_on in our probe). This is the one real behavioural difference of the probe on dada.
- Recommendation: keep NOT queued; record as a known functional difference. A pack already upgraded by a stock boot reports version >= 0x45 and the stock does nothing (INFERRED). Port only with: instruction-faithful transcription of the G1 path, symcmp4 0 only-stock / only-ours, a separate review, and a stock log from the target device.

### S5 `fg_set_fastcharge_mode(bq, false)` in the probe (both builds)
- VERIFIED: the function returns 0 BEFORE any bus access when `bq->fast_mode (+0x3ac) == enable`; fast_mode is 0 after the zeroing allocation and neither probe writes +0x3ac -> at probe it is a no-op in both (ours has the same early return, +0x2c..+0x38). Recommendation: keep (stock-identical).

## 2. New G17b code vs stock
- `fg_write_block`: stock form (ota_updating -> 0 without write; trylock + usleep_range(40,50), ota re-checked) VERIFIED; fg_set_force_report_full / fg_set_verify_digest are not in the 59-differing list (VERIFIED report).
- `fg_set_seal`: msleep(250) after each of the up to 3 seal commands, also after a failed write; 100 ms between attempts; VERIFIED in both binaries (3x msleep(0xfa) right after the line-5051 error log).
- `fg_read_vendor`: our jump table decodes to the same mapping as the stock: '4'->2, '5'->1, '7'->4 (MPC7012), 'C'->0 (TI), everything else incl. '6' and '8' -> 3 (MPC8011B); read failure leaves fg_vendor untouched in both (VERIFIED). The source comment "only '4','5','6','7','C' are distinct" is wrong for '6' (= default; cosmetic).
- `fg_read_fw_version`: MAC 0x02, 32 B; is_G2_new_version (MPC7012 only) and fw_ver (+0x148: 0x0001 -> 1, 0x0102 -> 2, else 0) VERIFIED equal (ours .text.bq_fg_probe+0xe68..0xec8 vs stock +0xbcf0..0xbd58).
- `fake_soh = -1` (+0x42c) VERIFIED; ui_soh 100, fake_temp -999, batt_fcc 2500000 equal.
- DT parse order and gates (support_nvt1000_ota u32, support-version-compatible, co_by_gpio / batt_ctl, base_termv(_gl), soh compensation incl. the UBSAN bound trap and "skip on batt_ctl failure") VERIFIED equal.
- 13 abnormal-info reads: commands, lengths, byte offsets, store-on-success VERIFIED equal; the log `__func__` differs ("fg_probe_read_abnormal_info" vs stock "fg_dump_battery_abnormal_info"; log text only).
- Gauge writes added by mistake: NONE. Our probe call set = stock minus {gpiod_direction_output_raw, 3x __fg_write_block, 4x __fg_read_word, 3x __fg_read_byte, 7x nfg1000_*, fg_set_force_sim_soh, queue_delayed_work_on, 8x msleep, 9x mutex_lock}; both have 21 MAC reads; the only write-capable callee is fg_set_fastcharge_mode (no-op at probe) (VERIFIED). MAC reads write the 2-byte subcommand to 0x3E as part of the read protocol, exactly as the stock.

## 3. Missing (20) and differing (59) functions: reachability on dada
Reachable in the stock on dada, absent here (missing functionality):
- `fg_term_voltage_update_work` (S4): every probe.
- cloud update chain: bq sysfs id 123 `upgrade_fw` store -> `check_fg_cloud_string` -> queue clound_update_work 15000 jiffies (60 s) -> `fg_clound_update_workfunc` -> `fg_fw_clound_update` -> `fg_fw_modify_ivterm_fun_P11` / `fg_ver43_update_ver_P11` (`GetCRC8` in the same chain: INFERRED). Only if userspace writes `upgrade_fw` (writer on dada unknown); our table has no such node (122 vs 123).
- first-usage-date write: fg_comp `strategy_fg_sysfs_store` -> `platform_fg_ops_set_first_usage_date` (ops slot NULL here -> error, no write; stock `fg_set_one_first_usage_date` -> `fg_write_first_usage_date`, MAC 0x7B) and bq sysfs id 35 (stock 0664, ours 0444). Only if userspace writes it.
Dead on dada (VERIFIED gates): `fg_set_force_sim_soh` (pv 3 / country 0; pv INFERRED), all 9 `nfg1000_*` incl. `nfg1000_ota_update_check` (magic flag only from the NVT probe branch), `fg_set_one_fast_mode_times` (fg_comp callers gated by `support-fast-mode-when-charging`: 0 hits in the dada DT; the bq sysfs `fast_mode_times` store, MAC 0xE5, exists in ours).
59 differing (spot checks of 8, the rest NOT reviewed):
- `fg_read_manufacturing_date`: REAL difference, read-only, reachable via ops / sysfs: the stock decodes two serial formats (SN byte 9 == '-' -> year from a letter table based at 2025, day from a 21-letter table; else the old digit format); ours implements only the old format -> a different date string for new-format packs.
- `fg_get_one_adapt_power`: stock override (pv == 1 and name "BP54" -> 90) missing; dead on dada if pv 2 (INFERRED).
- `fg_get_one_temp_max`, `fg_read_time_ht`, `fg_read_time_ot`, `fg_read_max_temp_occur_time`: ours keeps MPC7021 (vendor 5) branches that are unreachable since the G17b vendor fix, plus a different retry / log structure in temp_max (stock: up to 3 reads of MAC 0x60 with "retry: n" logs). Read-only.
- `fg_get_calc_rvalue`, `fg_get_batt_use_environment`, `fg_get_over_vol_duration`: size / layout differences, MAC commands equal at the call sites; not decoded further.

## 4. Defects found (none is a gauge write)
- D1 `fg_read_manufacturing_date` misses the stock new-SN-format decode (functional, read-only).
- D2 probe presence check ignores `support_nvt1000_ota` (dead on dada; wrong on an NVT target).
- D3 extra "FuelGaugw probe successfully" log after a failed device-name read (dead on dada).
- D4 abnormal-info log function name differs from the stock.
- D5 STATUS / source wording: fg_set_force_sim_soh called "data flash" (not proven; length-0 MAC command, no unseal); "3x raw __fg_write_block" are read-failure retries (normally 1); fg_read_vendor comment on '6'.
- D6 dead MPC7021 branches left in several readers (harmless now, but not stock form).

## 5. Before the module could leave DO NOT LOAD (ordered)
1. Rebuild from the current source (md5 926f2f84) and confirm the reviewed .ko f301f745 (not done here).
2. Fix D1 (port the stock decode) and re-run compare_ko / symcmp4 on it.
3. Decide BQ7-5 explicitly: keep absent with evidence from a stock log of the target device (version >= 0x45 = stock does nothing), or port the G1 path under a separate review.
4. Decide `upgrade_fw` and the first-usage-date write: keep absent (document) after checking which dada userspace component writes them.
5. Classify the remaining 51 unreviewed differing functions one by one (remove or justify the dead MPC7021 branches, retry structures), plus fg_sysfs_show / store 122 vs 123.
6. Confirm hwid platform_version / country and the gauge vendor byte from a stock boot log of the device (turns the INFERRED dead branches into facts).
7. Make the consumers match (fg_comp, business_charger_comp and the rest of the charging family at G14k+ versions), then a full-module compare pass.
8. Only then a monitored device test with a stock fallback; the module stays community-only, outside any ROM / kernel package.
