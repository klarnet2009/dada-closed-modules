EQUIVALENT (reference = OS4 stock `vendor_dlkm/sla.ko`)

Template: XRING `work/re/xiaomi-modules/mi_net/slm/{stats.c,sla_stats.h}` (dijun-v-oss `xiaomi-modules/mi_net/slm`). NOT the Unisoc `arctic-w-oss` sla (different driver; the port_targets.csv entry was wrong, the Xiaomi "sla" is the `net/sla/stats.c` family, same code as alioth/cmi/picasso).
Builds as-is against 6.6.144 (`LINUX_VERSION_CODE >= 5.10` proc_ops path, `pde_data`).

Only change: `strlcpy()` -> `strscpy()` (3 calls in `sla_interface_start`, inlined into `config_file_write`); the stock imports `strscpy` (CRC 0xdd64e639) and has no `strlcpy`. `strlcat()` stays.

compare_ko vs OS4 stock: 21 functions byte-identical, relocations, objects, 0 exports, 50 imports + CRCs, modinfo (`GPL v2`): all OK -> EQUIVALENT.
Note: the OS4 sla.ko is function-for-function identical to the HyperOS 3 one.
Build: ThinLTO `sla_defconfig`, `CONFIG_LTO=1`, `-ffunction-sections -fdata-sections`, 2 bazel iterations.
