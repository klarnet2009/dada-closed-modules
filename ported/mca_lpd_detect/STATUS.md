# mca_lpd_detect - EQUIVALENT (port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_hardware_monitor/mca_lpd_detect.c + inc/mca_lpd_detect.h
Result: EQUIVALENT: compiles on 6.6.144; function set (8), bytes (incl. __LINE__), relocs, export lpd_is_charging_limit + CRC, imports + CRC (26), modinfo (depends mca_sysfs,mca_platform_buckchg_class,mca_log), 7 data objects identical

- template used unchanged except: mca/common/mca_adsp_glink.h include dropped (the stock does not depend on it; the stock calls the buckchg class lpd getters) - its line is kept as a blank line so the __LINE__ numbering stays; SYSFS_DEV_5 = "hw_monitor" added to mca_sysfs.h
- buckchg provider: stubs_mcaA/include/mca/platform/platform_buckchg_class.h now declares all 83 exports (generated from the verified real module, scratch hdr_gen.py) and mca_platform_buckchg_class_stub.c is generated from it (stub_gen.py); all 83 stub CRCs equal the stock exports
