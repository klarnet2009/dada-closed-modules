# mca_parse_dts - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 11 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_parse_dts.ko`: compare_ko.py EQUIVALENT (11 functions, 11 exports, 11 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_parse_dts - EQUIVALENT (port task)


Template: XRING tree work/re/xiaomi-modules/mca/mca_common/mca_parse_dts.c (unchanged)
Result: EQUIVALENT: compiles on 6.6.144 without changes; function set (11), bytes (incl. all __LINE__), relocs, exports + CRC (11), imports + CRC (11), modinfo (depends mca_log), data identical

- stock == template (same source version); provider stub for mca_log only
