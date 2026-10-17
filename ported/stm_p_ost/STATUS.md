# stm_p_ost - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 7 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/stm_p_ost.ko`: compare_ko.py EQUIVALENT (7 functions, 0 exports, 15 imports).

Earlier headline (HyperOS 3 round): stm_p_ost - EQUIVALENT (compare_ko.py all OK)

Template: micode_bixi `drivers/hwtracing/stm/p_ost.c` (upstream Linux drivers/hwtracing/stm), copied UNCHANGED (+ `stm.h`) and built against 6.6.144 headers.
Needs //re/stm_core:stm_core as dep (import CRCs of stm_register_protocol/stm_source_* exports match). Build: ThinLTO defconfig, function/data sections, CONFIG_LTO=1.
