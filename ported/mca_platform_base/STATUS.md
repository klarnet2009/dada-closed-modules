# mca_platform_base - EQUIVALENT (agent mcaA, port task)

Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_base.c (unchanged)
Result: EQUIVALENT: compiles on 6.6.144 without changes; function set (5), bytes, relocs, imports + CRC (14), modinfo (depends mca_log), 5 data objects identical

- stock == template; with 6.6.144 (>= 6.6.9) the template's KERNEL_VERSION branch selects .probe = mca_platform_base_probe (one-arg i2c probe), same as the stock
