# mi_ubt - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 4 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mi_ubt.ko`: compare_ko.py EQUIVALENT (4 functions, 1 exports, 28 imports).

Earlier headline (HyperOS 3 round): mi_ubt - EQUIVALENT (compare_ko.py all OK: 4 functions, 1 export, 28 imports)

Template: micode_bixi `drivers/xiaomi/mi_ubt/mi_ubt.c` (+ `include/mi_ubt/mi_ubt.h` in stubs_portA), used unchanged except:
- 6.6.144 `__task_state_index()` additionally reports TASK_FROZEN as uninterruptible; the stock (6.6.57) did not. Local `mi_task_state_index()` copy of the 6.6.57 logic,
  fed into the header's `task_index_to_char()` (keeps the `task_index_to_char.state_char` object).
Build: ThinLTO `mi_ubt_defconfig`, `-ffunction-sections -fdata-sections`, `CONFIG_LTO=1`.
