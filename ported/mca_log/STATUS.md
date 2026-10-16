# mca_log - EQUIVALENT (agent mcaA, port task)

Template: XRING tree `work/re/xiaomi-modules/mca/mca_common/mca_log.c` (builds unchanged on 6.6.144; only the vendor headers `mca/common/*.h` come from `src/stubs_mcaA/include`).
Result: (a) compiles on 6.6.144, (b) function set (10), exports + CRC (5), imports + CRC (23), modinfo (depends mca_event,mca_sysfs) OK, (c) function bytes + relocations identical, 11 data objects identical (`cmpdata_mcaA.py`).

Changes relative to the XRING template (the stock is a newer version of the driver):
- log prefix now carries the pid: `"[%02d:%02d:%02d:%03ld-E][%5d]"` (E/I/D) with `task_pid_nr(current)`;
- new `time_offset` field (after `max_cache_num`, struct size 216, `info_level_flag` removed) and a new sysfs attribute `time_offset` (rw 0660, last in the table, id 10):
  show prints it, store logs `update time offset: %s` with `__mca_log_err` and stores `value / 1000`; `mca_log_get_cur_time` adds it to `tv_sec`;
- XRING specific parts removed: `/mca_xring_usb` + `mca_log_not_init` DT check, `info_level_flag` gates in the three log functions;
- `mca_log_exit` only removes the sysfs group (no buffer kfree).
Build: ThinLTO `defconfig` + `-ffunction-sections -fdata-sections`, provider stubs `mca_event_stub`, `mca_sysfs_stub` (new, `stubs_mcaA`).
