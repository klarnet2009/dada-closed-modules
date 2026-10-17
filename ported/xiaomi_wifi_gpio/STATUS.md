# xiaomi_wifi_gpio - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 8 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/xiaomi_wifi_gpio.ko`: compare_ko.py EQUIVALENT (8 functions, 0 exports, 40 imports).

Earlier headline (HyperOS 3 round): xiaomi_wifi_gpio - EQUIVALENT


`compare_ko.py`: 8 functions bytes+relocs, objects, 0 exports, 40 import CRCs, modinfo: all OK; `portC_data.py`: 8 data objects (sar_fam, sar_ops, sar_policy, sar_mcgrps, of_match table, platform_driver...) identical, kCFI identical; rodata strings identical incl. the WARN_ON file name.

Template: MiCode branch `goku-u-oss-test`, `drivers/misc/wifi_gpio/xiaomi_wifi_gpio.c` (the only branch carrying it; 6.1/5.15-era, but all APIs it uses are still valid on 6.6.144).
Changes against the template (found by diffing with the stock disassembly):
- `sar_send_status(uint32_t status)`: the `pid` parameter and the `genlmsg_unicast()` branch are gone; `genlmsg_put(msg, 0, ...)` and always `genlmsg_multicast(&sar_fam, msg, 0, 0, GFP_ATOMIC)`;
- the callers pass only the status; `wifi_gpio_driver_exit()` additionally calls `sar_send_status(0)` first (a `bl .text+0x38` with w0 = 0 in the stock);
- `hwid.h` is not in any public tree: local `hwid.h` with `enum { CountryCN = 0 }` and `uint32_t get_hw_country_version(void)`; the provider is our own reconstructed `//re/hwid:hwid` (depends=hwid matches).
- `#line 1 "drivers/misc/wifi_gpio/xiaomi_wifi_gpio.c"` as first line so `__FILE__` (WARN_ON bug table strings) equals the stock string (`-fmacro-prefix-map` through `copts` did NOT work).
Build: ThinLTO defconfig, `CONFIG_LTO=1`, function/data sections. `struct ant_gpio_data` is 160 bytes with the 6.6 android work_struct (48 B, KABI reserves) / delayed_work: no extra members needed.
