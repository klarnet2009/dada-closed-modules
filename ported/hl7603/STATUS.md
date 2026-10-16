# hl7603 - EQUIVALENT (all 8 functions byte-identical, relocations, objects, imports+CRC, modinfo)

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
