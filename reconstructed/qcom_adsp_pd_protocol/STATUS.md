# qcom_adsp_pd_protocol - EQUIVALENT vs OS4 (g5)

Stock reference: HyperOS 4.0 `qcom_adsp_pd_protocol.ko` (md5 9d4b1f48..., <work-dir>), checked 17.10.2026 by with the real Kleaf build (`//re/qcom_adsp_pd_protocol`) + compare_ko.py (EQUIVALENT).

unchanged since HyperOS 3: no source change; real Kleaf build + compare_ko.py vs the OS4 stock: function set (41), bytes, relocations, global objects, exports (0), imports+CRC (13), modinfo all OK.

---

## Earlier notes (HyperOS 3 round, kept for the history)

# qcom_adsp_pd_protocol - EQUIVALENT

Built with the ThinLTO defconfig. compare_ko: 41 functions byte-identical, relocations identical, global objects, imports CRCs (13 symbols) identical.
Stubs (own package re/stubs_misc: stub_mca + mca_headers): __mca_log_err/info, mca_event_block_notify_register,
mca_adsp_glink_read/write_prop, protocol_class_pd_register_ops.
Key points:
* __LINE__ of the mca_log calls is pinned with `#line N` (calls on one source line).
* struct protocol_class_pd_ops (50 slots, 400 bytes) and all helper structs come from the BTF of the stock
  vendor_dlkm/mca_protocol_pd_class.ko; the stub export CRC of protocol_class_pd_register_ops (0xd80afe9d) only matches with the
  exact C spelling of every type: `unsigned int` (not u32/uint32_t), `unsigned char *`, `bool`, `unsigned long s_secert[4], digest[4]`,
  enum uvdm_state WITHOUT `= N` except `CMD_INIT = 50, CMD_NAK = 100, REQUEST_PD_DR = 200, RESET_VSAFE0V = 0xFF`, trailing comma,
  pd_pdo{min_volt,max_volt,max_current}, adapter_power_cap{min_current,max_current,min_voltage,max_voltage,max_power}.
  Found by brute force with <build-dir> (genksyms emulator) against the stock export CRCs.
* devm_kzalloc (gfp 0xdc0), no MODULE_DEVICE_TABLE (stock has no alias), platform_driver .remove (not remove_new).
* info struct layout: dev@0, pd_active@8, pd_verifed@0xc, event_nb@0x10, abnormal_nb@0x28, verify_process@0x40 (0x48 bytes).
