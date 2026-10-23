# G17b NOTE (19.10.2026, review-batt-emerg D5): the boot event 85/0 sent by charger_partition_prepare goes ONLY to fg_comp and ARMS its battery-SN check (it does not mean 'no emergency pending'); the value is ignored; the call is lost if fg_comp has not registered yet. Comment-only change (no rebuild). COMMUNITY-ONLY, no safety claim.

# charger_partition - EQUIVALENT vs the GLOBAL OS4.0.0.7 stock (G14b 19.10.2026; function bytes/relocs/kCFI/objects/exports+CRC/imports all PASS, only buildmeta+depends differ as for every stub-provider build). COMMUNITY-ONLY: never flash, never in a ROM/kernel package: the charging family stays STOCK in any image.

## G14b (19.10.2026): updated from CN 0.6 to GLOBAL OS4.0.0.7.XOCMIXM (stock `<work-dir>`)
Changes global vs CN (stock vs stock, symcmp4 + BTF START_ID=141744 for the global modules / 141904 for CN): `charger_partition_info_1` grows 32 -> 68 bytes: `+32 batt_safe_lvl_data` (u32, accessed as two u16: low half = "batt_safe_vlim", high half = "batt_safe_ilim"; the split and names are INFERRED from the ldrh/strh offsets, units unknown), `+36 char batt_emerg_sn[32]`; `struct ChargerPartition` 240 -> 272 (info_2 +200 -> +236, rdy +232 -> +268); `get_info_1`/`set_info_1` now return 0/-1 (were void); alloc/read/write/dealloc size argument 32 -> 68 for info type 1; 6 new exports `charger_partition_{read,write}_batt_{safe_vlim,safe_ilim,emerg_sn}` (read vlim/ilim call get_info_1 first then test the pointer, read_sn tests the pointer first, write_sn ignores the set_info_1 result, write_vlim/ilim return it); `charger_partition_prepare` additionally calls `mca_strategy_func_process(6 /*FG*/, 85 /*MCA_EVENT_BATT_EMERG*/, 0)` before get_info_2; enum `mca_event_notify_list` gets `MCA_EVENT_BATT_SAFE_LVL = 84`, `MCA_EVENT_BATT_EMERG = 85`, MAX 86. The whole file shifts by +67 source lines for log pins from `charger_partition_work` on (re-pinned).
Hacks (listed, as before): two more genksyms-only prototype pads (`fcrceQCbkGSUnf` read_batt_emerg_sn, `fcrcluY2L5zU51` write_batt_emerg_sn; no natural spelling char */char[N] N<=130 reproduces the stock CRC); the write takes `const char *` (proved by the kCFI hash 0xb605e861). Headers: copy `work/re/src/stubs_portB_g` (global flavour of stubs_portB; CN modules keep using stubs_portB). The CN-0.6 source is kept as `charger_partition.c.cn06`.
Build: real Kleaf, 42 of 42 functions byte-identical to the global stock.

(older entries below describe the CN OS4.0.0.6 state)

G6 re-check against the HyperOS 4.0 stock (17.10.2026): OS3 and OS4 stock binaries compared function by function (g6/os3_vs_os4.csv): all 36 function bodies, relocations, kCFI words, data/rodata sections, exports+CRC, imports+CRC, strings and modinfo are identical (only vermagic/scmversion differ), so the existing source stays valid for OS4. Real Kleaf build of the source compared with the OS4 stock `vendor_ramdisk/charger_partition.ko`: compare_ko.py EQUIVALENT (36 functions, 19 exports, 36 imports). Community-only source (charging stack): never flash it, never put it into a ROM build.

Earlier headline (HyperOS 3 round): charger_partition - EQUIVALENT (36 functions byte-identical, relocations, objects, 19 exports + CRCs, 36 imports + CRCs, modinfo)


Template: XRING `work/re/xiaomi-modules/mca/mca_shared_memory/{charger_partition.c,inc/charger_partition.h}` (kernel API already 6.6: scsi_execute_cmd/exec_args). ~12 builds.
The dada stock is a later design: the partition blocks are cached in the driver struct and written through.
- struct `ChargerPartition` (size 0xf0): dev 0, sdev 8, delayed_work 0x10 (0x88 bytes on this kernel), part_number 0x98, part_name 0xa0, `charger_partition_info_1 info_1` 0xa8 (32 bytes: power_off_mode, zero_speed_mode, mishow, double85,
  remove_temp_limit, soc_limit, memory_test, reserved), `charger_partition_info_2 info_2` 0xc8 (eu_mode, ocd_count, hocd_count, cuv_count, hcuv_count, hscd_count, test, reserved), rdy 0xe8, not_notify_module 0xe9.
- New static helpers (noinline): `charger_partition_get_info_1/2` (alloc+read+copy into the struct+dealloc; the log prints the fields of the COPY), `set_info_1/2` (alloc+write+dealloc), `charger_partition_prepare`
  (reset info_1 to 2/2/0, notify DEBUG events 0x4c..0x4f for non-zero double85/remove_temp_limit/memory_test/soc_limit, get_info_2, EU strategy notifications).
- New exports: get_mishow, read/write_{double85,remove_temp_limit,memory_test,soc_limit,ocd_count,cuv_count,hscd_count(write is spelled hcsd)}; `read_remove_temp_limit` is NOT exported in the stock (global, no EXPORT_SYMBOL).
- work(): hwid based part number (`platform_version == 9 ? 25 : 22`), log of `get_hw_country_version()` (hwid.ko; uint32_t), no DT parse; alloc() lazily allocates rw_buf (kzalloc) + memset, dealloc() keeps rw_buf; remove() also frees rw_buf.
- Headers (stubs_portB/include/mca/shared_memory/charger_partition_class.h, hwid.h): enum values proven by the stock (HOST_KERNEL=4, HOST_LAST=8, INFO_1/2, INFO_LAST=11, magic 0x20240725).
- Export CRCs of alloc/dealloc/read/write: no natural spelling of the prototype reproduces them (324 spellings of u8/uint8_t/enum x u32/uint32_t/size_t tried, as found); a genksyms-only first declaration with an extra array
  parameter (forcecrc.py trick, `#ifdef __GENKSYMS__` at the top of the .c) gives the stock CRCs; the compiler never sees it.
- `#line` log pins by `portB_tools/pb_pin.py`.
