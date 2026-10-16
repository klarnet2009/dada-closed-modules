# sc8581 - EQUIVALENT (60 functions byte-identical, relocations, objects, 43 imports + CRCs, modinfo)

Template: XRING `work/re/xiaomi-modules/mca/mca_hardware_ic/charge_pump_ic/sc8581/{sc8581.c,inc/*.h}` (2506 lines). ~40 iterations incl. header work.
Stock vs template (all reconstructed from the stock disassembly):
- `hwid.h` include removed (only `mca_get_hwid_info()` used): new `mca_cp_is_en_buck_project_hwid()` in probe (platform_version==3 && hwid_value==0x10004 -> `client->addr = 0x6f`, logs "adjust en buck CP addr"/"normal CP addr", err log "get hwid info failed").
- `enum cp_model` has 4 values {SC85XX_UNKNOWN=0, SC8561, SC8581, SC8585=3} (stock probe stores 1/2/3; the bus-ovp switch has cases 1..3).
- New `bool vout2out_uvp_fix` (struct offset 0x1ae, DT `support_reverse_quick_charge`); `sc8581_set_key()` (reg 0x76 unlock/lock sequence 0x44,41,47,41,4d,41; the log "register 0x76 = 0x%x" has the `%s` fed by `val`: the log_tag argument is missing in the original source),
  `sc8581_set_vout2out_uvp()` (reg 0x84=3 between set_key(true/false)), called from `sc8581_set_pmid2outuvp_th` for SC8585 when the DT flag is set.
- `sc8581_enable_charge/enable_qb/enable_busucp/set_pmid2outuvp_th` return void (the ops wrappers return 0); `enable_busucp` also updates bit1 and bit3 of reg 0x07; `SC8581_PMID2OUT_UVP_BASE`=100, SC8585 branch `threshold>7 ? 5 : 0`.
- New ops: `ops_cp_set_revchg` (retry loop 10x with mdelay(20) around `sc8581_set_revchg()`; modes 5(reverse)/1(forward)), `sc8581_get_ovpgate_enable()` (REG_0B), `ops_cp_set_adjustadble_timeout` (cp_update_bits REG_0D mask 0xff); `ops_cp_set_default_mode`, `ops_cp_check_iic_ok` and the CP mode-change event removed.
- `sc8581_irq_handler` without the i2c_is_working early return; `sc8581_i2c_complete` tests `client` for NULL; dump_important_regs log `"%s: [0x%02X]..."`; init_device: reg 0x42=0x82 only for SC8585, one `sc8581_set_fsw(600)` (no role switch).
- Log `__LINE__`: pinned with `#line` by the new tool `portB_tools/pb_pin.py` (finds the stock call sites by tracking x0/x1/w2 before `bl __mca_log_*` and matches (__func__, format)).
Headers (in `re/stubs_portB`): `platform_cp_class.h` reconstructed from the stock provider BTF (struct size 392; note `cp_get_pmid2out_uvp_dis` takes `bool` by value, six getters take `u32 *`, param names/spelling checked via genksyms brute force: export CRC 0xce284cad matches), `mca_event.h` CP event numbers (0x15..0x24) proven by the stock log strings.
