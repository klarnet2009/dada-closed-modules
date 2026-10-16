# charger_partition - EQUIVALENT (36 functions byte-identical, relocations, objects, 19 exports + CRCs, 36 imports + CRCs, modinfo)

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
