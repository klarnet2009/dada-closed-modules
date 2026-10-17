# mi_stack - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 5 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mi_stack.ko`: compare_ko.py EQUIVALENT (5 functions, 0 exports, 25 imports).

Earlier headline (HyperOS 3 round): mi_stack - EQUIVALENT (compare_ko.py all OK: 5 functions, 25 imports)

Template: micode_bixi `drivers/xiaomi/mi_stack/mi_stack.c`, source unchanged except the lock-key hack: stock `.bss` is merged, so `mi_stack_init.__key` and `kbuf` share address 0
(relocs resolved to the key name). Fix: `portA_lockkeys.h` (stubs_portA) redefines mutex_init() with the key in `.bss.pa_lockkeys` and `kbuf` is put in the same section (key first).
