# hardwareinfo - EQUIVALENT

Template: XRING `xiaomi-modules/vendor_stability/hardwareinfo/hardwareinfo.c` (sysfs `/sys/kernel/hardware_info_to_dump/hardw_info`). Compiles unchanged on 6.6.144.
compare_ko: 4 functions identical, relocations, objects, 0 exports, 22 imports + CRCs, modinfo (`depends=minidump`): all OK -> EQUIVALENT.
The stock (Qualcomm build) additionally registers the 4 KiB buffer in Qualcomm minidump:
- `struct md_region md_entry = {0}; strlcpy(md_entry.name, "HARDW_INFO", sizeof(md_entry.name)); virt_addr = (uintptr_t)buf; phys_addr = virt_to_phys(buf); size = 4096;`
  `ret = msm_minidump_add_region(&md_entry); if (ret < 0) { pr_err("Failed to add hardw_info data in Minidump\n"); goto build_fail; }` (between the kzalloc and the kobject creation).
  Needs `local_defines CONFIG_QCOM_MINIDUMP=1` (header `soc/qcom/minidump.h` from the GKI tree) and the provider stub `re/stubs_portD:minidump_stub` (`minidump.ko`, CRC of `msm_minidump_add_region` matches the stock).
- the exit path no longer frees `hardwareinfo_buff` (it stays registered in minidump).
- Lesson: `strlcpy` (not `strscpy`/`strcpy`/`strncpy`) reproduces the 8+2+1 byte store pattern of the stock for the 11-byte name.
