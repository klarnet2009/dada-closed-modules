# mi_memory - EQUIVALENT (OS4 stock `vendor_ramdisk/mi_memory.ko`; two documented stub/offset hacks)

`compare_ko.py` on the real Kleaf/ThinLTO build (6.6.144, `//re/mi_memory:mi_memory`, package `work/re/src/mi_memory/`): 29 functions byte-identical, relocations identical, global objects (incl. the `.llvm.<hash>` ones) identical,
55 imports + CRCs identical, 0 exports, modinfo identical (description + two `MODULE_AUTHOR`, `depends=ufs_qcom` comes from the stub provider). 17.10.2026 (round 2, ~60 build/diff iterations with the Bazel-free ThinLTO loop `g3_tools/mm.sh`).
Build: `bash work/re/src/g3_tools/g3_build.sh mi_memory stubs_g3`. The module goes to the vendor ramdisk (first-stage), OS4 stock is function-for-function the same as HyperOS 3.

Template: MiCode `erhu-w-oss` `drivers/scsi/mi_ufs/mi-memory/{mem_interface,mi_dram_info,mi_memory_sysfs,mi_ufs_info,mv}.[ch]` (`work/re/tplm/erhu/`), which turned out to be close to the stock; the OS4 stock is a reduced/newer revision of it.
The a17D note "forked ufs_hba layout" is wrong: the stock offsets (dev@0x40, ufs_device_wlun@0x48, outstanding_tasks@0x70, ..., clk_gating.state@0xb80, ufs_stats.event[].cnt stride 0x70) are those of the GKI 6.6 `<ufs/ufshcd.h>`.

Changes against the template (all derived from the stock disassembly):
- mem_interface.c: 6.6 SCSI request API for `_scsi_execute` (`scsi_alloc_request(.., BLK_MQ_REQ_PM)`, `blk_rq_map_kern`, `blk_execute_rq(req, true)`; cmd_len forced to 16 only for 0xC0/0xD0), `rq_flags = RQF_PM`; the fortify warning text
  `field "scmd->cmnd" at drivers/mi_ufs/mi_memory/mem_interface.c:213` is reproduced with `#line 213 "drivers/mi_ufs/mi_memory/mem_interface.c"`; descriptor reads go through the Xiaomi `mi_ufshcd_read_desc_param` / `mi_ufshcd_query_descriptor_retry`;
  runtime PM is taken on `&hba->ufs_device_wlun->sdev_gendev` (stock: hba+0x48 + 0x1d8); `ufs_get_string_desc` returns 0 on failures and formats `serial:%02x%02x...\n` itself for `DEVICE_DESC_PARAM_SN`;
  `ufs_read_string_desc` calls a static `ufshcd_read_desc()` helper (pm get, read, pm put).
- mi_ufs_info.c: `dump_*_show` report read failures with `dev_err(hba->dev, "%s: Failed reading  dump_*_show, ret %d")` and return 0, `dump_string_desc_serial_show` just prints the `%s` of a 256 byte buffer, counters use `%lld`,
  host stats read through a local view struct at the stock offsets (0x5c..0xb4 of `hba->priv`), `err_state_show`/`err_reason_show` use `struct UFS_DATA {s64 err_occurred; char err_reason[10][32];}` from `get_ufs_data()`,
  `hr_show` takes `hr_lock`, supports only WDC/TOSHIBA/KIOXIA/SAMSUNG/MICRON/SKhynix/XBSTOR (YMTC/HG vendors removed), the SKhynix path first reads the product name (`HN8T05FEHKX073` selects the new CDB), `scsi_sk_hr` reads the product name too,
  `look_up_scsi_device` prints `[mi_ufs_info] scsi device proc name is %s`, no `soc/qcom/socinfo.h`.
- mv.c: the three ThinLTO-promoted objects are hidden globals with the stock names (`mv_proc_open.llvm.241927734351936473`, `mv_proc_fops.llvm.241927734351936473`, `.str.llvm.241927734351936473` = "mv").
- mi_memory_sysfs.c: `class_create(name)`, second `MODULE_AUTHOR("lilin <lilin16@xiaomi.com>")`.

Documented hacks:
1. `hba->dev_info.wspecversion` is read as a raw `u16` at hba+0xa18 (`HBA_WSPECVERSION`): the stock layout of `struct ufs_dev_info` inside the Xiaomi `ufs_hba` differs from GKI (GKI would give +0x1400).
2. The three imports `mi_ufshcd_read_desc_param` (0xcf664bd3), `mi_ufshcd_query_descriptor_retry` (0x4cf5dd0c), `get_ufs_data` (0xd45d0297) come from the stub provider `//re/stubs_g3:stub_ufs_qcom`, whose export CRCs are forged
   (`void sym(struct fgXXXXXX *)`, tags from `g3_tools/forge1.py`); the real prototypes involve the Xiaomi-forked `struct ufs_hba` and were not recovered (our local prototypes in `mem_interface.h` are only for compiling).
