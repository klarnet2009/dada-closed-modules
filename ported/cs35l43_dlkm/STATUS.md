# cs35l43_dlkm - EQUIVALENT (strict compare_ko_v2: STRICT-EQUIVALENT; one documented hack: forged CRC of the provider stub for the import `cs_dsp_load_coeff`)

## G15c (19.10.2026) - re-verified against the GLOBAL OS4.0.0.7 stock (first line unchanged)
Stock-vs-stock CN 0.6 -> global: STRICT-EQUIVALENT (module not changed). Real Kleaf build of this source against `<work-dir>` (g15c_tools/kb.sh): compare_ko_v2 VERDICT STRICT-EQUIVALENT, kCFI preamble 0 mismatches (97 functions). The dada device tree has cirrus,cs35l43 nodes at 0x40 and 0x42 (global dtbo): this is the smart PA actually used on dada. Hack list unchanged (forged CRC of the provider stub for `cs_dsp_load_coeff`).


Old first line (before the strict recheck G9): # cs35l43_dlkm - EQUIVALENT (OS4 stock; one documented hack: forged CRC of the provider stub for the import `cs_dsp_load_coeff`)

Reference: OS4 `<work-dir>`. `compare_ko.py` on the real Kleaf/ThinLTO build (6.6.144): function set 116 OK, function bytes ALL identical, relocations identical, globals identical,
32 exports + CRCs identical, 147 imports + CRCs identical, modinfo identical (reached 103/116, 17.10.2026 finished the last 13 functions).
Build: `bash work/re/src/g3_tools/g3_build.sh cs35l43_dlkm stubs_g3` (pushes the package + stubs_g3, Bazel build, compare). Quick loop: `g3_tools/cs.sh` (ThinLTO emulation + per-function diff).

Template: XRING `dijun-v-oss: xring-modules/drivers/audio/asoc/codec/xring_pa/cs35l43/*` (see the history below), reworked from the stock disassembly.

## What recovered from the OS4 binary (all 13 remaining functions)
- `wm_adsp_buffer_free`, `wm_adsp_compr_trigger` (inlined attach), `wm_adsp_event_post_run` (inlined `wm_adsp_buffer_init`/`parse_coeff`): the earlier `x != NULL &&` guards around list_for_each_entry bodies were
  codegen hacks that were wrong; the plain upstream loops (no NULL tests) match. In the two error paths of `buffer_parse_legacy/coeff` the stock does `if (buf->regions) kfree(buf->regions); kfree(buf);`.
- `cs35l43_delta_select_put`: stores `delta_requested = ucontrol->value.enumerated.item[0]` first, returns the result of `apply_delta_tuning` (or -EINVAL + dev_err "...due to DSP not run" with the item as argument when the DSP is not running).
- `delta_enum_text` has 17 entries (`null`, `<scene>`, `<scene>-fold` for music/voice/movie/voip/video/game/handsfree/handset); `apply_delta_tuning` checks the index as two separate tests (`< 0`, `>= 18`; one combined test folds the `tbnz`).
- `cs35l43_pcm_startup`: dev_dbg + inlined `cs35l43_set_dai_fmt(dai, CBS_CFS|DSP_A)` (fmt 0x4004) before the rate constraint; `cs35l43_pcm_hw_params`: dev_dbg at entry, no `set_dai_fmt`, no `FMTBIT_32` clamp, `dev_dbg` (not `dev_info`)
  for the wl/width/rate/channels line, sysclk = `2 * params_rate(params) * asp_width`.
- `cs35l43_check_mailbox` returns void; the MEM_VAL error with msg 0x6000004 logs `[TF-POP] Memory Validation error` and queues `err_work` unless `err_lock` is locked.
- `cs35l43_main_amp_event` POST_PMD: after `GLOBAL_ENABLES=0` also `ASP_ENABLES1=0`, then a 100 x 1 ms poll of `IRQ1_EINT_1` bit 16 (PDN done), `dev_dbg("PDN Done Status : %d, %s")`, clear bit.
- `cs35l43_log_dsp_err` (returns int): for ERROR / INVALID_STRM_CNT / ACT_BOOT_CNT it compares the value with the last seen one (three u32 at 0x378/0x37c/0x380), copies `[ERROR]`/`[INVALID_STRM_CNT]`/`[ACT_BOOT_CNT]` into `miev_err_reason`
  (devm_kzalloc(0x30) in probe), logs `stream arb err reg:[ERROR]` (sic, no space for the first) and returns -1. `cs35l43_irq`: NMI/MPU errors call `mievent_report(906001355, "Cirrus PA dsp NMI/MPU Error", dev, "NULL")`,
  the stream-arb error logs `[TF-STABILITY]Stream Arb Error type:%d, Error reg:%s ` and reports `Cirrus PA dsp Stream Arb Error` with `miev_err_reason`.
- `cs35l43_probe`: no XRING `gpio_len_enable`/`default` pinctrl block, reset pulse `usleep_range(2000,2100)` twice (no 200 ms), the rcv/spk pinctrl init (`rcv_gpio_high/low`) is inlined and its result ignored, `devm_kzalloc(dev, 0x30)` for
  `miev_err_reason` (non fatal), `err_miev:` label for the dai_driver allocation failure (frees the reason buffer only), first line is `dev_err("DEBUG cs35l43 probe E\n")`.
  `mutex_init` keys are put into one section (`.bss.cs35l43_lockkeys`) like in the other ThinLTO modules (stock merges them to `cs35l43_probe.__key`).
- Imports: "Digital PCM Volume" is `SOC_SINGLE_SX_TLV(..., 0x4CF, 0x391, dig_vol_tlv)` (min 0x4cf / max 0x391 read from the stock `.data..compoundliteral`), which adds the `snd_soc_{info,get,put}_volsw_sx` imports.

## Documented hack
Import `cs_dsp_load_coeff` (provider: OS4 stock `cs_dsp.ko`, a Cirrus-fork export that GKI lacks): the CRC 0x68be42d0 cannot be reproduced from any prototype spelling we tried (16 variants of return/firmware/file types, BTF says
`int (struct cs_dsp *, const struct firmware *, const char *)`; the other 29 exports of the GKI cs_dsp.c copy match the stock CRCs). The provider stub `//re/stubs_g3:stub_cs_dsp` therefore exports it with a forged
struct tag (`void cs_dsp_load_coeff(struct fgBNCdja *)`, `g3_tools/forge1.py`) so that its genksyms CRC equals the stock one. The module under test itself is compiled against the real prototype (`cs_dsp.h` in this package).

## History
XRING monolith glue removed, real i2c_driver/of_match/pm_ops, `cs35l43-i2c.c` -> `cs35l43_i2c.c`, Cirrus names `wm_adsp_*` restored, `MODULE_IMPORT_NS(FW_CS_DSP)`, product gating removed, `cs35l43_private` re-laid out to the stock offsets,
`cs35lxx_dai_drv_append_suffix`, `DSP RESET` control, `mievent_report` (`ErrReason`/`ErrRegister`, id 906001355), `[TF-STABILITY]`/`[TF-NOSOUND]` log prefixes (`a17C_tools/strpatch.py`), stubs `//re/stubs_a17C:stub_miev`.

## G9 strict recheck (18.10.2026): now STRICT-EQUIVALENT
compare_ko_v2 found data that compare_ko.py does not look at; recovered from the OS4 stock `.rodata`/`.data` (dump with relocations, tools in <work-dir> objrows.py, routes.py):
- DAI names: `dummy_dai.name` and `cs35l43_dai[0].name` are `"cs35l43"` in the stock (we had the XRING `"xring-spk"`; that string does not exist in the stock).
- `amc_enable_ctrl` (new `SOC_DAPM_SINGLE("Switch", SND_SOC_NOPM, 0, 1, 0)`, between amp_enable_ctrl and dre_ctrl) and widget `SND_SOC_DAPM_SWITCH("AMC Enable", SND_SOC_NOPM, 0, 0, &amc_enable_ctrl)` between "AMP Enable" and "DRE" (31 widgets).
- routes: `{"ASPTX1", NULL, "ASP TX1 Source"}` became `{"AMC Enable", "Switch", "ASP TX1 Source"}` + `{"ASPTX1", NULL, "AMC Enable"}` (112 routes).
- controls (29): two more at the end, after "Enable RCV Pin Control": `SOC_SINGLE("CLASSH CONFIG", CS35L43_CLASSH_CONFIG, 0, 0x7FFFFF, 0)` and `SOC_SINGLE("PCM Invert", CS35L43_AMP_CTRL, CS35L43_AMP_INV_PCM_SHIFT, 1, 0)`.
Effect on behaviour: the ASP TX1 path now goes through the AMC Enable switch, the DAI name is `cs35l43`.
Real Kleaf build (6.6.144): compare_ko_v2 `VERDICT: STRICT-EQUIVALENT` (even buildmeta passes for this module); old compare_ko EQUIVALENT (116/116 functions); kCFI preamble 0 mismatches.
