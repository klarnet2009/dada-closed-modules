# Audio amplifier / codec modules vs the GLOBAL OS4.0.0.7 stock (goal-G15c, 19.10.2026)

Reference: `<work-dir>` (global OS4.0.0.7). Method: real Kleaf build of work/re/src/<module> (tools work/re/src/g15c_tools: kb.sh = Bazel + compare_ko_v2 + preamble_cmp, kob.sh = multiset diff of the real .ko, dfn.sh/dfc.sh = one function of stock/built, strdiff2.sh = message-string diff, objstr.py = object/relocation dump, who.sh = which stock function uses a string; scratch <work-dir> bazel-out deleted; stub package work/re/src/stubs_g15c = copy of stubs_g3w). Nothing flashed or loaded, no git, no firmware/tuning blobs copied.

## 1. Stock vs stock (CN OS4.0.0.6 -> global OS4.0.0.7)
compare_ko_v2 (os4g/cmp/cn_vs_gl.csv, re-checked for aw882xx): **every audio module is STRICT-EQUIVALENT between CN 0.6 and global** (aw882xx, tfa98xx, sia91xx, sia91xx_tuning, cs35l43, fs19xx, tas25xx, machine_dlkm, audio_prm, audio_pkt, snd_event, swr*, lpass_cdc*, wcd9*, wsa88*; aw882xx differs only in the build-id, tfa98xx/sia91xx are byte-identical). Only the non-audio modules in the 24-module code-change list differ. So all earlier CN-based audio work is valid against global without rebuilding the sources; this round re-measured everything against the global files.

## 2. What the dada hardware uses
The global dtbo and all vendor_boot DTBs contain exactly one smart-PA family: `cirrus,cs35l43` (two nodes, 0x40 / 0x42), plus `qcom,wsa883x`, `wsa884x`, `wcd939x`. There is **no** awinic / sia91xx / tfa / foursemi / ti,tas25xx node: aw882xx, tfa98xx, sia91xx, fs19xx, tas25xx are never bound on dada (they belong to other Xiaomi devices sharing the vendor image). The one amplifier that matters on dada, cs35l43_dlkm, is STRICT-EQUIVALENT.

## 3. Status table (Kleaf, real build vs global stock)
| module | status | functions identical | other compare_ko_v2 | kCFI | notes |
|---|---|---|---|---|---|
| cs35l43_dlkm | EQUIVALENT (STRICT) | 97/97 (all) | all PASS | 0 diff | used on dada; 1 hack: forged CRC of the provider stub (cs_dsp_load_coeff) |
| fs19xx_dlkm | EQUIVALENT (metadata only) | 124/124 | srcversion + depends order differ | 0 | hacks: forged member name for fsm_get_config CRC, hidden .llvm globals, u8 `& 1` form |
| tas25xx_dlkm | EQUIVALENT after crc_patch.py | 112/112 | Kleaf alone: 2 export CRCs differ (patched in the package) | 0 | hack: crc_patch.py on 2 exports |
| sia91xx_tuning_dlkm | EQUIVALENT (metadata only) | 5/5 | only .note.Linux differs | 0 | no hacks |
| aw882xx_dlkm | COMPILES-only | 251/267 (16 differ) | set, objects, imports (88), exports, modinfo PASS | 235/235 | no BEHAVIOUR difference found; 16 = CODEGEN; stores into aw_device for all 8 chips equal (emulator) |
| tfa98xx_dlkm | COMPILES-only | 281/389 (108 differ; was 277) | set, objects (now PASS), imports (109), exports PASS; anon-data/bug-table/altinstr FAIL | 375/375 | this round: 6 BEHAVIOUR fixes; open items below |
| sia91xx_dlkm | COMPILES-only | 66/104 (38 differ; was 65) | set, imports (90), exports (17) PASS; objects: content of all stock objects equal, extra static g3m_rst_off_level; depends order | 92/92 | this round: 3 BEHAVIOUR fixes (scene list, DAI rates/channels, messages) |
Modules with no source in work/re/src (stock in the package): machine_dlkm, audio_prm, audio_pkt, snd_event, swr*, lpass_cdc*, wcd9*, wsa88*, bt_fm_swr, hdmi (machine_dlkm and miev exist only as stub providers for our builds).

## 4. BEHAVIOUR differences fixed this round (source edits in work/re/src, all compiled and compared)
tfa98xx_dlkm: (1) of_device_id table `tfa,*` (16 entries) instead of 12 `nxp,*`; (2) tfa_dev_get_state: third MANSTATE position for rev 0x66; (3) tfa_dev_set_state: FAIM/MTP sequencing (`(is_calibration && !probus) || rev==0x13`, no set_mtp, system_stable print); (4) tfaRunStartup: is_calibration from the `.cal` profile name + TDM bit-width block; (5) tfaRunSpeakerStartup/Boost: entry print, error/bypass prints; (6) tfa9912/tfa9888 factory trimmer + vboost fixup verbose prints; four error-text strings.
sia91xx_dlkm: (1) ALSA enum "Sipa Audio Scene" now has the stock six scenes (Receiver/Factory were mapped to the wrong scene numbers); (2) DAI rates 8k..96k (0x7fe) and channels_max 4; (3) hw_params / chip-id messages.
aw882xx_dlkm: none found (strings identical, objects PASS, store-state emulation equal, remaining 16 functions CODEGEN).

## 5. Classification of the remaining differences
- CODEGEN (equal behaviour): aw882xx all 16; tfa98xx tfaRunStartDSP (stock inlines tfa98xx_dsp_write_tables as an ops call with NULL check), tfa_dsp_cmd_id_coefs, tfaRunStartup/SpeakerStartup line-number immediates (__LINE__), debugfs get/set functions (frame/order), jump-table anon chunks; sia91xx sipa_cmd_show (frame size, one extra snprintf), _copy_from_user.
- BEHAVIOUR / unknown, still open: tfa98xx: stock-only messages for dbgfs mtpex/otc/start set ("Can only clear MTPEX", "Unable to check DSP access", "Calibration profile not found"), "[goodix] ... init_count=%d", "no_start=%d", "tfa_cf_enabled==0" in tfa98xx_i2c_probe; tfa_dev_set_state 10 words, tfa9894_specific 26, tfaContWriteFilesProf 23, tfa98xx_container_loaded 23; sia91xx: "component is NULL" check, the kcontrol put with `%u/channel`, sipa_probe 34 (chip-type masks 0x3a919c0), sipa_i2c_probe 17, sipa_compatible_chips_adapt 25, sipa_suspend 18.

## 6. Hacks (all remain listed in each STATUS.md)
Forged export-CRC attribute under `#ifdef __GENKSYMS__` in the machine_dlkm stub (CRC 0x77aedcd7 of set_smartpa_codec), `__builtin_expect_with_probability` in aw882xx_request_firmware, `#line 704/738` in tfa tfaContWriteFile, asm barrier + `g3m_rst_off_level` in sipa_resume, crc_patch.py for tas25xx, guessed field names (tfa_device pcm_format/bitwidth/verbose, aw/sia struct members) with offsets from the disassembly, trailing `\n` of new log strings (strings(1) hides it) and log levels taken from the stock string prefix.

## 7. Rule for all three COMPILES-only modules
DO NOT LOAD / not for flashing before an independent review. On dada they are not bound (no DT node), so keeping the stock files in the package is the safe choice; ours are only for completeness of the "all closed modules" goal.
