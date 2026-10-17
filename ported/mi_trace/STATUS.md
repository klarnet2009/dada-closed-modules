# mi_trace - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 14 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/mi_trace.ko`: compare_ko.py EQUIVALENT (14 functions, 0 exports, 35 imports).

Earlier headline (HyperOS 3 round): mi_trace - EQUIVALENT (compare_ko.py all OK: 14 functions, 35 imports incl. msm_minidump_{add,remove}_region CRCs, depends=minidump)

Template: micode_bixi `drivers/xiaomi/mi_trace/{mi_trace.c,mi_trace.h}`, source unchanged. Needs `CONFIG_QCOM_MINIDUMP=1` (otherwise <soc/qcom/minidump.h> turns msm_minidump_* into inline stubs and the calls vanish)
and a provider of the msm_minidump_* exports: `//re/stubs_portA:minidump_stub` (stub; real provider is the stock minidump.ko, see ../minidump).
