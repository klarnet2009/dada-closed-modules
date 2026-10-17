# mca_lpd_detect - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 8 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_lpd_detect.ko`: compare_ko.py EQUIVALENT (8 functions, 1 exports, 26 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_lpd_detect - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_lpd_detect.c + inc/mca_lpd_detect.h
Result: EQUIVALENT: compiles on 6.6.144; function set (8), bytes (incl. __LINE__), relocs, export lpd_is_charging_limit + CRC, imports + CRC (26), modinfo (depends mca_sysfs,mca_platform_buckchg_class,mca_log), 7 data objects identical

- template used unchanged except: mca/common/mca_adsp_glink.h include dropped (the stock does not depend on it; the stock calls the buckchg class lpd getters) - its line is kept as a blank line so the __LINE__ numbering stays; SYSFS_DEV_5 = "hw_monitor" added to mca_sysfs.h
- buckchg provider: stubs_mcaA/include/mca/platform/platform_buckchg_class.h now declares all 83 exports (generated from the verified real module, scratch hdr_gen.py) and mca_platform_buckchg_class_stub.c is generated from it (stub_gen.py); all 83 stub CRCs equal the stock exports
