EQUIVALENT (reference = OS4 stock `vendor_dlkm/mi_thermal_interface.ko`)

Template: MiCode `ziyi-s-oss` `drivers/thermal/mi_thermal_interface.c` (kernel 5.4, 1087 lines, the largest/most complete of the ~20 branches that carry this file; the `w`/`v` branches only have empty stubs). Copy of the original: `<work-dir>` (server).
The stock (HyperOS 3 and OS4 are function-for-function identical) is a newer revision than the template, so the source was rewritten from the stock disassembly:
- new sysfs attrs (in stock order): `torch_level`, `torch_real_level` (init -1), `thermal_max_brightness`, `display_therm_temp`, `voice_limit`, `super_hdr`, `cloud_game`, `dynamic_tj`, `ntn_limit`, `boot_complete`; removed `modem_rate`.
- second device `power_save` on the same "thermal" class (`mi_powersave_dev`, attrs `powersave_mode`, `power_level`, both init -1; `of_find_node_by_name(NULL, "power_save")`), created/destroyed like `thermal_message`.
- `sconfig` store also calls `mca_smartchg_set_scene(val)` (mca_smart_charge.ko); `board_sensor_temp` store calls `mca_event_block_notify(6, 62, &temp)` (mca_event.ko).
- `struct cpufreq_device` embeds `struct freq_qos_request qos_req` at +0x30 (no per-CPU `qos_req`); cpu_thermal_init frees on error paths.
- panel callback is the reduced one (`bool screen_light/screen_state`, no log spam, no secondary panel); panel client is `PANEL_EVENT_NOTIFIER_CLIENT_THERMAL` (=6 in the Xiaomi `panel_event_notifier.h`, local copy).
- `ATOMIC_ATTR`/`BUF_ATTR` macros generate the identical show/store pairs; `create_class_node()` is `__always_inline` and shared by the two node creators (stock inlines the same code twice).
- the unused globals `board_sensor` / `ambient_sensor` stay global (symtab).
- local copy of `drivers/base/base.h` (for `to_subsys_private`); `thermal_core.h` is not needed.

Build: ThinLTO `mi_thermal_interface_defconfig`, `local_defines` CONFIG_LTO/CONFIG_DRM_PANEL/CONFIG_MI_THERMAL_MULTI_CHARGE/CONFIG_QCOM_PANEL_EVENT_NOTIFIER, function/data sections. Providers are CRC stubs in `work/re/src/stubs_a17D/` (`panel_event_notifier`, `mca_event`, `mca_smart_charge`; `void mca_smartchg_set_scene(int)` gives the stock CRC 0xcf9a3246).
compare_ko vs OS4 stock: 71 functions byte-identical, relocations, objects, 0 exports, 47 imports + CRCs, modinfo: all OK -> EQUIVALENT (~6 build/compile iterations).
Runtime depends on `panel_event_notifier`, `mca_smart_charge`, `mca_event` modules (as stock `depends=`), none of them is replaced by this port (the stubs must never be flashed).
