# fs19xx_dlkm - PARTIAL: COMPILES (Bazel/ThinLTO, GKI 6.6.144), 98 of 128 functions byte-identical, 21 differ, 9 missing (OS4 stock)

Reference: OS4 `<work-dir>` (FourSemi FS19xx smart PA, v4.0.7; ~20 quick-loop iterations + 3 Bazel builds). Not EQUIVALENT: imports, 9 functions and 21 bodies differ.

Template: MiCode `veux-r-oss: techpack/audio/asoc/codecs/fs19xx/{fs19xx.c,fsm_core.c,fsm_firmware.c,fsm_i2c.c,fsm_codec.c,fsm_misc.c,fsm-dev.h,fsm_public.h}` (FourSemi **v4.0.3**, Android R / kernel 5.4 era; fetched into the bare repo `<work-dir>`). The stock is v4.0.7 (20230609).
`fsm_firmware.c`, `fsm_misc.c`, `fsm_codec.c` are `#include`d by `fsm_i2c.c` in the original (one translation unit): kept as `*.inc.h`.

Changes relative to the template:
- 5.4 -> 6.6 porting: `pr_warning`->`pr_warn`, `FW_ACTION_HOTPLUG`->`FW_ACTION_UEVENT`, `symmetric_rates`->`symmetric_rate`, no `devm_gpio_free`, `probe(client)` / `void remove`, K&R prototypes, `MIN` redefinition.
- Config switches in `fsm-dev.h` as in the stock: no `CONFIG_FSM_Q6AFE` / `CONFIG_FSM_SYSFS` (so `fsm_q6afe.[ch]`, `fsm_sysfs.c`, `fsm_class.c` and the APR/RE25 ioctls are gone; the removed files are in `work/re/src/tpl_a17C/`), no `FSM_DEBUG` / `FSM_DEBUG_I2C`; `fs1958_*` renamed `fs19xx_*`, firmware name `fs19xx.fsm`, `FS1958_DEV_ID` = 0x29.
- Machine hook: `check_smartpa_type("fs19xx")` at probe start ("other smartpa type already set, no need to probe") and `set_smartpa_type("fs19xx", 7)` after a successful detect (stub `//re/stubs_a17C:stub_machine`; `check_smartpa_type`/`set_smartpa_type`/`get_smartpa_type` have the stock CRCs, the `set_smartpa_codec` prototype is not recovered).
- ThinLTO-promoted statics defined as hidden globals with the stock names (`g_fsm_config.llvm.15073763127247982342`, `g_fsm_mutex.llvm.6922001480050156751`, `g_fsm_pdev.llvm.6922001480050156751`, `g_fsm_fw_init.llvm.10112469624167539498`).
- `struct fsm_config` re-laid out to the stock offsets (extra u16 at 20, 8 bytes before the flag word, one more flag word: members `unrecovered_*`); version/date strings v4.0.7 / 20230609; `%d`->`%u` in 4 log formats (`a17C_tools/fmtpatch.py`).

Missing in the rebuilt module (new in v4.0.7, not in the template): `fs_mievent_report` (miev event), `fsm_amp_get/put`, `fsm_ultrasound_get/put` (kcontrols), `fsm_spksw_gpio_get/put` (speaker switch kcontrol), `fsm_get_index_by_position`, the out-of-line `_copy_from_user`. So the imports `cdev_tevent_*`, `gpiod_*_cansleep`, `strcpy` are absent.
Differing bodies (21): `fsm_i2c_probe/remove`, `fsm_misc_{open,read,write,ioctl}`, `fsm_codec_{register,probe}`, `fsm_hw_params`, `fsm_probe`, `fsm_init_dev_list`, `fsm_speaker_{onn,off}`, `fsm_set_{scene,volume}`, `fs19xx_{start_up,shut_down,pre_calib}`, `fsm_config_vol`, `fsm_get_rstrim`, `fsm_stub_switch_preset`, `fsm_parse_preset`, `fsm_firmware_init`: v4.0.7 logic changes.
