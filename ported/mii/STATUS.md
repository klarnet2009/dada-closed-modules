# mii - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 GKI (6.6.77) and OS4 GKI (6.6.118) stock mii.ko compared function by function: all 10 function bodies, relocations, data sections, 7 exports+CRC, 7 imports+CRC, modinfo identical (only vermagic/scmversion differ). Real Kleaf build of the unchanged source compared with OS4 stock system_dlkm/mii.ko: compare_ko.py EQUIVALENT (10 functions, 7 exports, 7 imports).

Earlier headline (HyperOS 3 round): mii - EQUIVALENT (compare_ko.py all OK: 10 functions, 7 exports, 7 imports)

Stock = GKI system_dlkm `mii.ko` (6.6.77), i.e. upstream `drivers/net/mii.c`; template micode_bixi `drivers/net/mii.c` (identical to Linux 6.6.x).
Differences to the template / build:
- exports: stock exports only 7 of 10 (KMI-trimmed: `mii_check_gmii_support`, `mii_check_link`, `mii_ethtool_sset` are global but NOT exported) -> those 3 EXPORT_SYMBOL lines removed.
- NOT a ThinLTO build: GKI system_dlkm modules are plain (LTO_NONE) builds; with the ThinLTO defconfig `generic_mii_ioctl` differs by one `eor` operand order, without it everything is identical.
  So no defconfig / CONFIG_LTO here (try the default non-LTO build first for GKI-built modules).
