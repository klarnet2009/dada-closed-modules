# mca_platform_base - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 5 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_platform_base.ko`: compare_ko.py EQUIVALENT (5 functions, 0 exports, 14 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_platform_base - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_platform/mca_platform_base.c (unchanged)
Result: EQUIVALENT: compiles on 6.6.144 without changes; function set (5), bytes, relocs, imports + CRC (14), modinfo (depends mca_log), 5 data objects identical

- stock == template; with 6.6.144 (>= 6.6.9) the template's KERNEL_VERSION branch selects .probe = mca_platform_base_probe (one-arg i2c probe), same as the stock
