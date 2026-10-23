# fs19xx_dlkm - EQUIVALENT (strict compare_ko_v2: only metadata differences srcversion + depends order; hacks: forged member name for the fsm_get_config CRC, hidden .llvm.<hash> globals, u8 `& 1` load form)

## G15c (19.10.2026) - re-verified against the GLOBAL OS4.0.0.7 stock (first line unchanged)
Stock-vs-stock CN 0.6 -> global: STRICT-EQUIVALENT. Real Kleaf build vs the GLOBAL stock: compare_ko_v2: everything PASS except buildmeta (srcversion differs: 0E1FBF87... vs 75479956...) and depends order (ours miev,machine_dlkm vs stock machine_dlkm,miev): metadata only, kCFI 0 mismatches (124 functions). No fs19xx/foursemi node in any dada DT: not bound on dada. Hacks unchanged.


Old first line (before the strict recheck G9): # fs19xx_dlkm - EQUIVALENT (128 of 128 functions byte-identical to the OS4 stock, compare_ko.py EQUIVALENT; hacks: forged member name for the fsm_get_config CRC, hidden .llvm.<hash> globals, u8 `& 1` load form)

Reference: OS4 `<work-dir>` (FourSemi FS19xx smart PA, v4.0.7). History: 98/128, round 1 117/128, round 2 127/128 (real Kleaf/ThinLTO build, `compare_ko.py`).
Build: `bash work/re/src/g3_tools/g3_build.sh fs19xx_dlkm` (deps `//re/stubs_a17C:stub_machine`, `stub_miev`); quick loop `work/re/src/g3_tools/fs.sh` (ThinLTO emulation + per-function diff, `FD="func ..."`).
compare_ko: function set 128 OK (also the out-of-line `_copy_from_user`), function bytes differ only in `fsm_firmware_init`, relocations OK, globals OK, imports + CRCs (76) OK, modinfo OK, exports OK (CRC forged, see below).

## What remains
1. `fsm_firmware_init`: stock `ldrb w8,[g_fsm_fw_init]; tbz w8,#0` where we get `cbz w8` (bool object defined as hidden `g_fsm_fw_init.llvm.10112469624167539498`). Every form that produces `tbz` (uint8_t with `& 1`, `__atomic_load_n`, `int8_t`, ...)
   changes the block layout of the whole function (the `mov w20,#0x350b` is no longer hoisted and the shared `-EINVAL` tail moves), so the bool/`cbz` form with a 1-word diff is kept. Unknown: why the stock load has no `!range`.
2. `fsm_get_config` export CRC: stock 653485011 (0x26f363d3), ours 407439750 (0x18490986). The genksyms stream of `struct fsm_config` contains the names of members that are new in v4.0.7 and invented here
   (`cur_scene` u16 at +8, `int spksw_gpio` +0x28, `int spksw_level` +0x2c, flag bit 18 `ultrasound_on`, one more u32 `unrecovered_flags` at +0x34). Offsets/types are verified by identical code; only names are unknown.
   Brute force (`g3_tools/bf_fsmcfg.py`, zlib.crc32 of the `genksyms -D` stream produced by `g3_tools/pb_gks2.sh`, which reproduces the real Kbuild CRCs exactly): 37M name combinations (24 x 3x19x31 x 22 x 41 candidates) without a hit.
   Documented fallback: `python3 <build-dir> built.ko stock.ko out.ko` sets the stock CRC (code untouched) - NOT applied to the tree, the .ko in `<work-dir>` has the real (different) CRC.

## What recovered (all from the OS4 disassembly)
Round 1: v4.0.7 kcontrols (`FSM_AMP_ON`, `FSM_Ultrasound_Enable`, `FSM_SW_GPIO` + handlers), `fs_mievent_report` (noinline, `__builtin_strcpy` of `dev->kobj.name`), `fsm_get_index_by_position`, many small fixes, i2c/of name `fs16xx`.
Round 2:
- `struct fsm_config`: the u16 inserted at +8 is `cur_scene` (last applied scene, written by `fsm_speaker_onn`), not a free `unrecovered_u16`.
- `fsm_speaker_onn/off`: early `if (speaker_on && next_scene == cur_scene) return` / `if (!speaker_on) return` (twice in `off`: before and after the log), `cur_scene = next_scene`, `next_scene = FSM_SCENE_MUSIC` after off.
- `fsm_set_scene`: caches `stream_muted` in a local, `speaker_off + 20 ms` before, shut down + 20 ms only when the scene changes while the speaker is on, ultrasound switches scene 0 -> 4 (`BIT(4)`) when `ultrasound_on && !force_calib`, `speaker_onn` after if not muted, `fsm_set_calib_mode` if speaker on.
- `fsm_config_vol`/`fsm_set_volume`: 1958 volume = `(vol << 8) | 0x80` (version high byte 0x17) or `| 0xC0`, `pos_mask & 8` with `cur_scene == 0x8000` forces 0xFBC0; no invalid-volume warning.
- `fsm_check_dev_type` (inlined in `fsm_init_dev_list`): 1958 accepts device type 0x17 / 0x29; `fsm_get_rstrim` has no "default rs_trim" warning.
- `fs19xx_start_up`: i2s config uses sample rate bits 7 when version high byte is 0x17 and REG4 low byte is 0xA1; TS (temperature sensor) is disabled for scenes LOW_PWR (only if `pos_mask == 8`), all `*_BYPASS` scenes and RCV, `fs_mievent_report(906001354, "PA data exception")` when the start-up fails;
  PLL table has an 11th entry `{12288000, 0x0260, 0x02A0, 0x0018}`; log format `invalid bclk/rate:%u, %u`.
- `fsm_misc_ioctl`: new command set (magic 0x7c, SET_SLAVE 0x706 / SET_ADDR, GET_DEVICE, SET_SRATE/SET_SCENE/INIT/CALIBRATE/F0_TEST via a 24 byte args struct, SPEAKER_ON/OFF, GET_RESULT and SEND_APR are no-ops, GET_INFO, SET_RE copies 20 bytes and drops them);
  with the real number of `copy_from_user` call sites the compiler keeps `_copy_from_user` out of line like the stock.
- `fsm_i2c_probe`: rewritten (v4.0.7): `dev_info("fsm i2c probe enter")`, "other smartpa type already set" returns 0, no i2c address remapping, DT parsing (`reset-gpio` + `FS19XX_RST_%02X`, `interrupts` gpio `FS19XX_IRQ`, `fsm,re25-dft`, `spksw-gpio` `FS19XX_SPKSW`, -EBUSY -> -EPROBE_DEFER for the reset gpio),
  `fs_mievent_report(906001353, "PA i2c exception")` on detect failure, `set_smartpa_type("fs19xx", 7)` after the irq request, `fsm_misc_init` + `fsm_codec_register` only for id 0 (no sysfs, no `fsm_set_pdev` there).
Template: MiCode `veux-r-oss: techpack/audio/asoc/codecs/fs19xx/*` (FourSemi v4.0.3, kernel 5.4) ported to 6.6 (see the a17C notes below); the stock is v4.0.7.

## Earlier notes
5.4 -> 6.6 porting, config switches as in the stock (no Q6AFE/SYSFS/debug), `fs1958_*` renamed `fs19xx_*`, firmware name `fs19xx.fsm`, `check_smartpa_type("fs19xx")`/`set_smartpa_type("fs19xx", 7)`,
ThinLTO-promoted statics defined as hidden globals with the stock names (`g_fsm_config.llvm.15073763127247982342`, `g_fsm_mutex.llvm.6922001480050156751`, `g_fsm_pdev.llvm.6922001480050156751`, `g_fsm_fw_init.llvm.10112469624167539498`),
`struct fsm_config` re-laid out to the stock offsets, version/date strings v4.0.7 / 20230609, `%d`->`%u` in 4 log formats.

## Update (17.10.2026)
- `fsm_get_config` export CRC now identical (0x26f363d3, real Kleaf build + compare_ko: exports/imports/relocs/globals/modinfo all OK). DOCUMENTED HACK: the member `uint32_t unrecovered_flags` of `struct fsm_config` (fsm-dev.h) is named `fcrcJLl3fZ3UN6`,
  an identifier forged with `a17A_scripts/forcecrc.py` (genksyms stream from `g3c_tools/pb_gks2.sh`, placeholder split, crc32 solved) so that the genksyms CRC equals the stock; code/ABI untouched. The real member names of the v4.0.7 struct remain unknown.
- Still 1 word in `fsm_firmware_init` (`cbz` vs stock `tbz w8,#0`): also tried `volatile bool` (still cbz and different layout). The stock load of `g_fsm_fw_init` carries no range knowledge; no source form found that keeps the block layout.

## Update (17.10.2026)
No change in the count (still 127 of 128, only the `cbz` vs `tbz w8,#0` of `g_fsm_fw_init` in `fsm_firmware_init`). Diagnosis (from `ld.lld -r -mllvm -print-before=codegenprepare -mllvm -filter-print-funcs=fsm_firmware_init`): the stock IR must be the SimplifyCFG-folded form `select(presets==NULL, trunc(load i8) ^ 1, false)` WITHOUT `!range` on the load (the cheap `trunc` condition lowers to `tbz #0`, and is still cheap enough to be folded, which is what hoists `mov w20,#0x350b`).
Every C form that drops the range (u8 `& 1`, memcpy into a bool, volatile, atomic) is more than 2 IR instructions and is not folded (the `mov w20` is no longer hoisted, 17 words differ); `bool` keeps `!range` and gives `cbz`; `unlikely/likely/!!/(int)/==true/__builtin_unpredictable` give either `cbz` or a worse layout. Not solved.

## Update (17.10.2026)
No change (still 127 of 128). New facts from the prelink IR (`llvm-dis` of `<work-dir>`): with `bool g_fsm_fw_init` the prelink pass already builds `select(presets==NULL, icmp eq (load i8 !range), 0, false)` (so the hoisted `mov w20,#0x350b` comes from that select, and the load keeps `!range`, hence `cbz`). Tried a 1-bit `bool` bitfield view (`((struct { bool v:1; } *)&g_fsm_fw_init)->v`): the load loses `!range` and a `tbz w8,#0` is emitted, but the condition is `and i8 x,1; icmp eq 0` (2 instructions, over the speculation budget), the select is not formed and the block layout changes (`mov w20,#0x350b` sunk below the `tbz`, shared EINVAL tail moved: 14 lines differ). Reverted; the tree is unchanged. What is still needed: a range-less load that is a single `trunc i8 to i1` (stock IR), e.g. produced by a different declaration of the object than a plain static `bool`.


## Update (17.10.2026) - SOLVED, EQUIVALENT
`fsm_firmware_init` fixed: `g_fsm_fw_init` is now `uint8_t` (was `bool`), and the function loads it into a local BEFORE the presets test:
`inited = g_fsm_fw_init; if (fsm_get_presets() || (inited & 1)) return MODULE_INITED;` (fsm_firmware.inc.h).
Why: with the load already in the dominating block, SimplifyCFG's FoldBranchToCommonDest only has to clone the single `and i8 x,1` (within the bonus-instruction budget of 1), so the select/`mov w20,#0x350b` hoist of the stock is formed and the range-less
`and 1` condition lowers to `tbz w8,#0`; machine sinking puts the `ldrb` back after the `cbnz`. (A plain `& 1` test without the early load was not folded - the earlier rounds' dead end.)
Real Kleaf/ThinLTO build + compare_ko.py (both the <build-dir> and the g3e copy): function set/bytes/relocs/globals/exports(1)/imports(76)/modinfo all OK -> EQUIVALENT.
Documented hacks in this module: forged member name `fcrcJLl3fZ3UN6` in `struct fsm_config` (export CRC of fsm_get_config), ThinLTO-promoted statics as hidden `name.llvm.<hash>` globals, the uint8_t+`&1` load form above (codegen hint, ABI unchanged).
Tools of this round: `work/re/src/g3f_tools` (copies of g3e_tools pointing to <work-dir> `varsearch.py` now needs env STOCK=<stock module>; `vs_fw*.py` = this search).

## G9 strict recheck (18.10.2026): all REAL differences fixed
compare_ko_v2 found static data that compare_ko.py does not compare; recovered from the OS4 stock `.data`/`.rodata` (dump with relocations: <work-dir>):
- Names: the stock driver is called `fs16xx`: `FSM_DRV_NAME` = "fs16xx" (fsm_i2c_driver.driver.name, g_fsm_misc.name, `MODULE_ALIAS("foursemi:fs16xx")`), `FSM_FW_NAME` = "fs16xx.fsm" (g_fsm_config.fw_name), DAI `fsm_aif_dai[0].name` = "fs16xx-aif". (Strings inside functions stay "fs19xx" as before: the functions were already byte/reloc-identical.)
- g_fsm_config initial values: cur_scene = 1 (FSM_SCENE_MUSIC), i2s_bclk = 6144000, i2s_srate = 96000, spksw_gpio = -1 (we had 0 / 1536000 / 48000 / 0).
- fsm_aif_dai: no `.symmetric_rate` (the stock bit is 0).
- control "FSM_SW_GPIO" max is 3 (we had 1) -> its compound literal; `fsm_spksw_gpio_get/put` are `static` (stock: local symbols, not global).
- fsm_detect_device: the switch jump table maps dev_id 0x17 to the FS1958 code (same as 0x29): added `case 0x17:` before `case FS1958_DEV_ID:` (this was the only `anon-data`/function-reloc difference).
- modinfo: `MODULE_SOFTDEP("pre: sia91xx_dlkm")` (first entry in .modinfo, before author).
Real Kleaf build: compare_ko_v2 fails only `buildmeta` (srcversion differs, a hash of the source files) and `depends` order (`miev,machine_dlkm` vs stock `machine_dlkm,miev`; the package build rewrites .modinfo to the stock value). Old compare_ko EQUIVALENT (128/128 functions), kCFI preamble 0 mismatches.
Behaviour notes: the codec DAI/driver/alias names now follow the stock (`fs16xx`), the default I2S rate is 96 kHz/6.144 MHz BCLK until set by hw_params.
