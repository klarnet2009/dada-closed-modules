# mi_wmark - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 6 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mi_wmark.ko`: compare_ko.py EQUIVALENT (6 functions, 0 exports, 14 imports).

Earlier headline (HyperOS 3 round): mi_wmark - EQUIVALENT (compare_ko.py all OK: 6 functions, 35 imports)

Template: micode_bixi `drivers/staging/mi-perf/mi_wmark/{mi_wmark.c,mi_wmark.h}`, unchanged except `MODULE_INFO(staging, "Y")` (stock modinfo has `staging=Y`, added by modpost for drivers/staging; not done by the DDK build).
