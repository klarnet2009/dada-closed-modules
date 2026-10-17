# hardwareinfo - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 4 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_dlkm/hardwareinfo.ko`: compare_ko.py EQUIVALENT (4 functions, 0 exports, 22 imports).

Earlier headline (HyperOS 3 round): hardwareinfo - EQUIVALENT


Template: XRING `xiaomi-modules/vendor_stability/hardwareinfo/hardwareinfo.c` (sysfs `/sys/kernel/hardware_info_to_dump/hardw_info`). Compiles unchanged on 6.6.144.
compare_ko: 4 functions identical, relocations, objects, 0 exports, 22 imports + CRCs, modinfo (`depends=minidump`): all OK -> EQUIVALENT.
The stock (Qualcomm build) additionally registers the 4 KiB buffer in Qualcomm minidump:
- `struct md_region md_entry = {0}; strlcpy(md_entry.name, "HARDW_INFO", sizeof(md_entry.name)); virt_addr = (uintptr_t)buf; phys_addr = virt_to_phys(buf); size = 4096;`
  `ret = msm_minidump_add_region(&md_entry); if (ret < 0) { pr_err("Failed to add hardw_info data in Minidump\n"); goto build_fail; }` (between the kzalloc and the kobject creation).
  Needs `local_defines CONFIG_QCOM_MINIDUMP=1` (header `soc/qcom/minidump.h` from the GKI tree) and the provider stub `re/stubs_portD:minidump_stub` (`minidump.ko`, CRC of `msm_minidump_add_region` matches the stock).
- the exit path no longer frees `hardwareinfo_buff` (it stays registered in minidump).
- Lesson: `strlcpy` (not `strscpy`/`strcpy`/`strncpy`) reproduces the 8+2+1 byte store pattern of the stock for the 11-byte name.
