# mi_ubt_test - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 3 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mi_ubt_test.ko`: compare_ko.py EQUIVALENT (3 functions, 0 exports, 5 imports).

Earlier headline (HyperOS 3 round): mi_ubt_test - EQUIVALENT (compare_ko.py all OK: 3 functions, 5 imports incl. dump_user_backtrace CRC)

Template: micode_bixi `drivers/xiaomi/mi_ubt/test/mi_ubt_test.c`, unchanged, built with CONFIG_MI_UBT=1 (calls dump_user_backtrace) and depends on //re/mi_ubt (needs `visibility` on it).
