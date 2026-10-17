# hl7603 - EQUIVALENT vs OS4

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 8 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/hl7603.ko`: compare_ko.py EQUIVALENT (8 functions, 0 exports, 20 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): hl7603 - EQUIVALENT (all 8 functions byte-identical, relocations, objects, imports+CRC, modinfo)


Template: XRING `work/re/xiaomi-modules/mca/mca_hardware_ic/boost_ic/hl7603/{hl7603.c,inc/hl7603.h}`.
Changes needed (reconstructed from the stock disassembly; the dada build is newer than the XRING copy):
- debugfs table grew from 1 to 5 attrs: `vout`(0664) `chip_id`(0444, reg 0) `address`(0664) `count`(0664) `data`(0664); struct `boost_bypass_dev` got `int address` (0x34) and `int count` (0x38);
  `data` show loops `min(count, 6 - address)` registers with `scnprintf(read_buf[10], "%02x: %02x\n")` + `strcat` (fortified strcat/strnlen -> imports `fortify_panic`, `strnlen`, `memcpy`);
  store uses `kstrtoint(buf, 0)`, logs `mca_log_info("write attr[%d]: %d\n")` first, `data` write only if address < 6.
- probe sets `info->count = 1` after registering the panel notifier.
- `#line N` pins `__LINE__` of every log call. The min() must be written as `if (count < 6 - address) max = count; else max = 6 - address;` (any other spelling changes register allocation).
- Includes `<mca/common/mca_panel.h>` dropped (enum MCA_EVENT_PANEL_HBM_STATE_CHANGE=0x41 added to `stubs_portB/include/mca/common/mca_event.h`).
Build: ThinLTO defconfig, `CONFIG_LTO=1`, `CONFIG_DEBUG_FS=1`, function/data sections; stubs `//re/stubs_portB:stub_all` (copy of stubs_mcaB).
Iterations: 7.
