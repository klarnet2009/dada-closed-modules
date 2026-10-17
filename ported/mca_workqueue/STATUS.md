# mca_workqueue - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 7 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mca_workqueue.ko`: compare_ko.py EQUIVALENT (7 functions, 5 exports, 9 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): mca_workqueue - STATUS: EQUIVALENT


Template: XRING `mca/mca_common/mca_workqueue.c` (unchanged, 83 lines).  ThinLTO defconfig + function/data sections as for the other modules.
Provider stub: `st_mca_log` (stubs_mcaB).

Real build + compare_ko: function set 7, bytes identical, relocations identical, objects, 5 exports with CRCs, 9 imports with CRCs, modinfo -> EQUIVALENT (first build, no changes needed).
