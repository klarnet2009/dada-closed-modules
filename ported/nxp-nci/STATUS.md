# nxp-nci - EQUIVALENT vs OS4 stock (43 functions byte-identical, relocations, objects, 76 imports + CRCs, modinfo)

Reference: OS4 `<work-dir>` (modinfo name `nxp_nci`, qcom,sn-nci NXP SN1xx/SN2xx NFC driver). Agent a17C, ~12 quick-loop iterations + 2 Bazel builds.
Template: OnePlus13 `op13_modules: vendor/nxp/opensource/driver/nfc/{common.c,common.h,common_nxp.[ch],common_qcom.c,ese_cold_reset.[ch],i2c_drv.[ch]}` + `config/gki_nfc_conf.h`, uapi `nfcinfo.h`, `sn_uapi.h`.
NOTE: the `drivers/nfc/nxp-nci` tree in micode_dada/bixi/miro is the UPSTREAM Linux nxp-nci driver (different driver, not the stock one); the XRING `nfc-mi` driver (MiCode `dijun-v-oss`) is not it either (only its `print_rw_buffer` helper matches).
Changes relative to the template (reconstructed from the stock disassembly):
- All VBAT-monitor code removed (`nfc_vbat_monitor.[ch]`, `platform_gpio.vbat_irq`, `nfc_dev.nfc_vbat_monitor`, its init/free/irq-wake calls); the stock struct layouts (gpio at 0x184, cdev at 0x1f0...) only match without them. `CONFIG_NXP_NFC_VBAT_MONITOR` is NOT defined.
- OnePlus-only parts removed: `soc/oplus/boot/boot_mode.h` factory-mode VEN-off block in probe, `checkNfcChip()` / `nxp_nfc_read_id_properties()` chipset-id matching, the extra `usleep_range(NFC_GPIO_SET_WAIT_TIME_US)` at the start of `nfcc_hw_check()`.
- Timing: `NFC_GPIO_SET_WAIT_TIME_US` = 10000 (set_valid_gpio); `gpio_set_ven()` waits 10000..10100 us for value 1 and 20000..20100 us for value 0; `WAKEUP_SRC_TIMEOUT` = 100 (not 500).
- `nfc_parse_dt`: the two "irq %d" / "irq %d, ven %d, dwl %d, clkreq %d" messages are `pr_info`.
- New global `print_rw_buffer(const unsigned char *buf, const int len, bool is_send)` (from XRING nfc-mi i2c_drv.c; "####SEND/RECV" log; 258*2+1 byte buffer, loop bound `min(len, 258)`, `snprintf(.., 3, "%02X")`); never called in the stock.
- No `-DDYNAMIC_DEBUG_MODULE` (the stock has no `__dynamic_pr_debug`; pr_debug is compiled out). `NFC_SECURE_PERIPHERAL_ENABLED=1`, `CONFIG_NXP_NFC_I2C=1`, `CONFIG_IPC_LOGGING=1` as local defines.
- Lock keys: `inc/nxp_lockkeys.h` redefines `init_waitqueue_head`/`mutex_init` so keys land in explicit sections (`.bss.nxp_misc` together with `nfc_post_init.post_init_success`, `.bss.nxp_probe` for the four probe keys) to reproduce the stock ThinLTO .bss layout (relocation targets `nfc_misc_register.__key`, `nfc_i2c_dev_probe.__key`).
- Providers are stubs in `re/stubs_a17C`: `stub_smcinvoke` (`get_client_env_object`), `stub_pinctrl_msm` (`msm_gpio_mpm_wake_set`), `stub_ipc_logging` (`ipc_log_context_create/destroy`, `ipc_log_string`); import CRCs match.
- Build: ThinLTO `nxp-nci_defconfig`, `CONFIG_LTO=1`, function/data sections.
Tools: `work/re/src/a17C_tools/` (`a17c_push.sh`, `q.sh`, `nxp.sh`).
