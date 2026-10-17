# aw882xx_dlkm - COMPILES-only: builds (Bazel/ThinLTO, GKI 6.6.144); 71 of 267 functions byte-identical, 167 differ, 29 missing (OS4 stock)

g3g 17.10.2026: not touched (no better template found; the T545 kernel only has the old `aw8896`). Still COMPILES-only; the stock needs the newer vendor SDKs described below.

Reference: OS4 `<work-dir>` (AWINIC AW882XX smart PA; ~6 iterations). Not EQUIVALENT.

Template: OnePlus13 `op13_modules: vendor/oplus/kernel/audio/codecs/aw882xx/` (QCOM techpack flavour). NOTE: the inventory entry `aw882xx_v1.13.0` (MTK flavour, `aw882xx_device_*` names, needs `mtk-sp-spk-amp.h`) is the wrong tree; the un-versioned `aw882xx/` has 239 of the 267 stock function names.
Build settings: `OPLUS_FEATURE_SPEAKER_MUTE`, `CONFIG_MIEV=1`, `-Wno-visibility -Wno-incompatible-pointer-types`; `AW_MTK_PLATFORM_WITH_DSP` disabled in `aw882xx_dsp.h` (the stock has neither the MTK nor the QCOM DSP path, i.e. the stubbed `aw_send_afe_*` branch); stubs `stub_miev`, `stub_machine` (`check/set_smartpa_type`).

Why it differs: the stock is a newer AWINIC SDK than the OnePlus snapshot: `aw882xx_dev_*` / `aw882xx_dsp_*` names (template `aw882xx_device_*`), generic dB conversion tables (`aw_direct_*`, `aw_6_0P125_*`, `aw_6_0P5_*` instead of `aw_pid_<n>_*`), `aw882xx_algo_auth_*` + `aw_algo_auth_misc_*` misc device, `vol_offset` / `voltage_offset` controls, `iv_output`, `aw882xx_spksw_gpio_*`, `mievent_report`, `aw_get_irq_nobst_type`, and different `struct aw_device` / `aw_cali_desc` layouts (e.g. profile index at +0x238 vs +0x1f8 in the template). 85 template-only (OnePlus pid/mute/proc) functions are extra.
If someone continues: reconstruct the struct layouts from the stock offsets (`pb_fd.py` diffs show them), rename `aw882xx_device_*` -> `aw882xx_dev_*`, drop the OnePlus feedback/proc code, add the missing 29 functions.
