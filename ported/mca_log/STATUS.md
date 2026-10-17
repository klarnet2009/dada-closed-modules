# mca_log - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 10 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_log.ko`: compare_ko.py EQUIVALENT (10 functions, 5 exports, 23 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_log - EQUIVALENT (port task)


Template: XRING tree `work/re/xiaomi-modules/mca/mca_common/mca_log.c` (builds unchanged on 6.6.144; only the vendor headers `mca/common/*.h` come from `src/stubs_mcaA/include`).
Result: (a) compiles on 6.6.144, (b) function set (10), exports + CRC (5), imports + CRC (23), modinfo (depends mca_event,mca_sysfs) OK, (c) function bytes + relocations identical, 11 data objects identical (`cmpdata_mcaA.py`).

Changes relative to the XRING template (the stock is a newer version of the driver):
- log prefix now carries the pid: `"[%02d:%02d:%02d:%03ld-E][%5d]"` (E/I/D) with `task_pid_nr(current)`;
- new `time_offset` field (after `max_cache_num`, struct size 216, `info_level_flag` removed) and a new sysfs attribute `time_offset` (rw 0660, last in the table, id 10):
  show prints it, store logs `update time offset: %s` with `__mca_log_err` and stores `value / 1000`; `mca_log_get_cur_time` adds it to `tv_sec`;
- XRING specific parts removed: `/mca_xring_usb` + `mca_log_not_init` DT check, `info_level_flag` gates in the three log functions;
- `mca_log_exit` only removes the sysfs group (no buffer kfree).
Build: ThinLTO `defconfig` + `-ffunction-sections -fdata-sections`, provider stubs `mca_event_stub`, `mca_sysfs_stub` (new, `stubs_mcaA`).
