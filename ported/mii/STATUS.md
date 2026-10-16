# mii - EQUIVALENT (compare_ko.py all OK: 10 functions, 7 exports, 7 imports)
Stock = GKI system_dlkm `mii.ko` (6.6.77), i.e. upstream `drivers/net/mii.c`; template micode_bixi `drivers/net/mii.c` (identical to Linux 6.6.x).
Differences to the template / build:
- exports: stock exports only 7 of 10 (KMI-trimmed: `mii_check_gmii_support`, `mii_check_link`, `mii_ethtool_sset` are global but NOT exported) -> those 3 EXPORT_SYMBOL lines removed.
- NOT a ThinLTO build: GKI system_dlkm modules are plain (LTO_NONE) builds; with the ThinLTO defconfig `generic_mii_ioctl` differs by one `eor` operand order, without it everything is identical.
  So no defconfig / CONFIG_LTO here (try the default non-LTO build first for GKI-built modules).
