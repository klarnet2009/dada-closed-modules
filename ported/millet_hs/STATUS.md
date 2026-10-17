# millet_hs - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 5 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/millet_hs.ko`: compare_ko.py EQUIVALENT (5 functions, 0 exports, 8 imports).

Earlier headline (HyperOS 3 round): millet_hs - EQUIVALENT


Template: XRING `dijun-v-oss` `xring-modules/drivers/mihw/millet/millet_hs.c` (+ `millet.h`, `uapi/millet.h`; copies in `work/re/ref/portD/dijun_millet/`). Compiles unchanged on 6.6.144.
compare_ko: 5 functions byte-identical, relocations, objects, 0 exports, 8 imports + CRCs (from our `millet_core` build, `deps = //re/millet_core`), modinfo (`depends=millet_core`): all OK -> EQUIVALENT.
Only change in the shared header: see millet_sig (`frozen_task_group` has no `frozen_task()` term; `frozen_task` made `static inline` to satisfy -Werror; unused here).
