# mca_platform_bc12_class - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 8 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_platform_bc12_class.ko`: compare_ko.py EQUIVALENT (8 functions, 1 exports, 9 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_platform_bc12_class - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_bc12_class.c
Result: EQUIVALENT: compiles on 6.6.144; function set (8), bytes, relocs, export + CRC, imports + CRC (9), modinfo (depends mca_protocol_class,mca_log,mca_protocol_pd_class), 5 data objects identical

- the stock is an OLDER version than the template: no bc12_det_done op (platform_bc12_class_ops has 2 ops, 16 bytes) and no adapter_bc12_det_done in g_bc12_ops (adapter_protocol_class_ops, 96 bytes) - removed; added linux/of.h + linux/platform_device.h; '#line 157' pins the probe log
- headers: enum bc12_role (MAIN/AUX/MAX=2) in platform_bc12_class.h, TYPEC_PORT_1 in protocol_pd_class.h; protocol_class.h replaced by the fuller reconstruction of (adapter_protocol_class_ops, adapter_power_*), stub mca_protocol_class_stub now exports all 13 protocol_class_* functions (all 13 CRCs verified against the stock mca_protocol_class)
