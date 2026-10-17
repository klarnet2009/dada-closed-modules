# miicmpfilter - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 10 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_dlkm/miicmpfilter.ko`: compare_ko.py EQUIVALENT (10 functions, 0 exports, 16 imports).

Earlier headline (HyperOS 3 round): miicmpfilter - EQUIVALENT


Template: XRING `xiaomi-modules/mi_net/miicmpfilter/` (dijun-v-oss), used UNCHANGED (5 files: `miicmpfilter_core.c`, `miicmpfilter_nl.c`, 2 headers). Same vendor code, no platform glue.
compare_ko: 10 functions identical, relocations, objects (incl. `imcp_unreach_genl_family` in `.data..ro_after_init`, ops/policy tables), 0 exports, 16 imports + CRCs, modinfo (`GPL v2`, author, description, `rtnl-link-miicmpfilter` alias, version 1.0): all OK -> EQUIVALENT.
Builds as-is against 6.6.144 (`.resv_start_op` path of the `#if LINUX_VERSION_CODE > 6.1` is taken).
