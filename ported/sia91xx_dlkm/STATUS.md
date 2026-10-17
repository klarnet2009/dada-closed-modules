# sia91xx_dlkm - COMPILES-only: builds (Bazel/ThinLTO, GKI 6.6.144); 8 of 104 functions byte-identical, 62 differ, 34 missing (OS4 stock)

g3g 17.10.2026: not touched (no better template found; the T545 kernel only has the old `aw8896`). Still COMPILES-only; the stock needs the newer vendor SDKs described below.

Reference: OS4 `<work-dir>` (SI-IN SIA81xx/91xx smart PA; the separate `sia91xx_tuning` module is EQUIVALENT elsewhere), .

Template: MiCode `yunluo-s-oss: sound/soc/codecs/sia81xx/*` (Android S, kernel 5.4: old SIA81xx driver; ref `refs/remotes/o/yunluo-s-oss` of `<build-dir>`). Copies of the tuning/afe/socket files not used by the stock are in `work/re/src/tpl_a17C/sia_tuning/`.
Changes: pre-5.10 uaccess compat (`sia_compat.h`: `mm_segment_t`, no-op `get_fs/set_fs`, `filp_open` -> `-ENOSYS`; the template only wraps commented-out `kernel_write` with them), defines `SIA91XX_TYPE`, `SIPA_COMPILE_TO_MODULE`, `CONFIG_MIEV`, one K&R prototype fix; `sipa_tuning_cmd.c` and `sipa_tuning_if_dev.c` are kept only so that the template links (the stock has none of the `sipa_tuning_*` code and no `tuning_if_opt`).
Why it differs: the stock is a much newer, Xiaomi-restructured driver: multi-channel API (`sipa_multi_channel_*`, `sipa_get_channels`), `sia91xx_*` detect/start/mute, `sia91xx_spksw_gpio_*`, `mievent_report`, `check/set_smartpa_type` + `set_smartpa_codec` from machine_dlkm, firmware via `request_firmware` instead of file I/O, `sipa_param_*` without the writers, `crc32`, module init/exit. 34 stock functions are not in the template, 72 template-only functions (tuning/cal/set_vdd/timer) are extra. A real port needs the newer SI-IN source, which is not in the available MiCode branches.
