# mca_bmd - EQUIVALENT (12 of 12 functions byte-identical, relocations, objects, 0 exports, 33 imports + CRCs, modinfo; + G2)


SAFETY: charging module (`mca_*`). The source is for the community only; it must never be flashed or put into a ROM build.

Reference binary: `<work-dir>` (identical copy in vendor_dlkm), vermagic 6.6.57-android15-8. OS3 and OS4 stock differ in one function only (11 of 12 identical), so this reconstruction applies to both.
Template: XRING tree `work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_bmd.c` + `inc/mca_bmd.h`. The dada stock is a much newer/different source: the template only gave the scaffolding (sysfs show, driver/init glue, `get_btb_status` frame).

Result (real Kleaf build on 6.6.144, ThinLTO defconfig, `compare_ko.py`): function set OK (12), relocations OK, global objects OK, 0 exports OK, 33 imports + CRCs OK, modinfo OK.
Function bytes: all 12 identical (incl. all `__LINE__` immediates, pinned with `#line`).

Reconstructed from the stock disassembly (all of it differs from the template):
- struct `mca_bmd_dev` (0x1e0 bytes): dev, `bmd_scheme[2]` {scheme, channel, gpio, cfg_failed} (24 bytes each at +8), three delayed works (monitor +0x38, `request_hw_resource_work` +0xc0, `delay_report_bmd_sts_work` +0x148), `btb_online[2]` +0x1d0, `batt_missing` +0x1d2, `int fake_batt` +0x1d4, `fg_type` +0x1d8 (DT `fg_type`, 1 = dual battery / parallel).
- New functions: `mca_bmd_request_hw_resource_work` (retries gpio request for BTB gpio schemes, max 3 times, 100 ms), `mca_bmd_delay_report_bmd_sts_work` (initial notify 10 s after probe), `mca_bmd_get_status` (status 0x16 -> batt_missing). `mca_bmd_process_event` no longer sets pinctrl states (event 0x50 re-runs the delayed report); monitor interval 2000 ms.
- `mca_bmd_get_btb_status`: `cfg_failed` check, IIC scheme uses `strategy_class_fg_dual_is_chip_ok(index)` when fg_type == 1 (int result, `> 0`), else `strategy_class_fg_is_chip_ok()`.
- The XRING `subpmic_dev_notify()` / subpmic header and `strategy_class_fg_ops_is_init_ok()` probe-defer logic are NOT in the dada stock (removed).
- Workfunc: dual battery (`fg_type == 1`) reports `CHARGE_DFX_DUAL_BATTERY_MISSING (19)` with the master btb state, else `CHARGE_DFX_BATTERY_MISSING (14)`; fake battery logic differs by fg_type; notify `MCA_EVENT_BATTERY_FAKE_POWER = 0x12` (added to `stubs_a17A/.../mca_event.h`).

(Earlier open item `mca_bmd_probe` is solved, see the G2 update below.)

Providers for the build: stubs_a17A generated stubs (`gen_stub.py`: wireless class, strategy_fg_class, strategy_class), stubs_mcaA (log, event, parse_dts, buckchg), stubs_mcaB (charge_mievent), real mca_sysfs.
Build: `bash work/re/src/a17_push.sh mca_bmd` then on the server `<work-dir> mca_bmd` (own stock dir `<work-dir>`).

G2 update (17.10.2026): `mca_bmd_probe` solved with three source changes (the instant ThinLTO loop and the real Kleaf build via `build.sh mca_bmd` both say EQUIVALENT, no forged CRC, no padcrc, no codegen hack other than the documented `#line` pins):
1. In the GPIO case of the inlined `mca_bmd_parse_dt()` the nested if/else is followed by `ret = 0;` (stock zeroes w22 after gpio_request failure, direction failure and success, but NOT on the invalid-gpio path), so a failed gpio request is a deferred retry, not a probe failure.
2. `retry` is NOT an out-parameter: `mca_bmd_parse_dt(info)` itself ends with `if (retry) schedule_delayed_work(&info->request_hw_resource_work, msecs_to_jiffies(RESOURCE_RETRY_MS)); return ret;`. With an out-param (`bool *retry_out`) the zext/icmp-eq round trip made InstCombine invert the phi (`mov w23,#1` = "no retry"), which broke the block layout of the whole function (268 words); this single change took the diff to 12 words.
3. `info->batt_missing = 0;` is executed BEFORE `INIT_DELAYED_WORK(&info->monitor_bmd_work, ...)` in probe.
Reusable lesson: when a stock function shows a retry/flag in a callee-saved register whose polarity is the opposite of the source, look for a bool out-parameter that LLVM turns into `icmp eq (zext x), 0` and fold the consumer into the callee instead.
