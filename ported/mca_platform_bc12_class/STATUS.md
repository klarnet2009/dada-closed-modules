# mca_platform_bc12_class - EQUIVALENT (port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_bc12_class.c
Result: EQUIVALENT: compiles on 6.6.144; function set (8), bytes, relocs, export + CRC, imports + CRC (9), modinfo (depends mca_protocol_class,mca_log,mca_protocol_pd_class), 5 data objects identical

- the stock is an OLDER version than the template: no bc12_det_done op (platform_bc12_class_ops has 2 ops, 16 bytes) and no adapter_bc12_det_done in g_bc12_ops (adapter_protocol_class_ops, 96 bytes) - removed; added linux/of.h + linux/platform_device.h; '#line 157' pins the probe log
- headers: enum bc12_role (MAIN/AUX/MAX=2) in platform_bc12_class.h, TYPEC_PORT_1 in protocol_pd_class.h; protocol_class.h replaced by the fuller reconstruction of (adapter_protocol_class_ops, adapter_power_*), stub mca_protocol_class_stub now exports all 13 protocol_class_* functions (all 13 CRCs verified against the stock mca_protocol_class)
