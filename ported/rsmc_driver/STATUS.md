# rsmc_driver - EQUIVALENT (compare_ko.py all OK: 91 functions, 1 export, 77 imports)
Template: micode_bixi `drivers/rsmc/{rsmc_chiptype_select.c,module_type.h,x801/*}` (BeiDou short-message chip driver), functionally unchanged. Only ThinLTO naming/layout emulation (see stubs_portA/include/portA_thinlto.h, portA_lockkeys.h):
- promoted hidden globals `name.llvm.<hash>`: g_nl_ctx (msg_loop TU hash 9866079119346183571), g_rsmc_spi_ctx (2046493130197020927), g_smc_core + g_rsmc_spi_driver + functions smc_free_gpio/rsmc_tcxo_power_free (11734744625934473405);
- promoted string literals (`.str.llvm.*`, `.str.14.llvm.*`, `.str.62.llvm.*`) and `__func__.<fn>.llvm.*` arrays: replaced by explicit `const char x[] PA_SYM("<stock name>")` + `_printk(x, ...)` at the users that the stock imported across TUs
  (rsmc_stop_x801 "%s: enter", rsmc_update_soc_{rx,tx}_jiffies "%s", rsmc_spi_init_x801 "%s: call spi_register_driver ret %d");
- g_rsmc_fw_ctx put in `.bss.pa_fwkey` together with the mutex_init key of rsmc_device_reg_x801 (stock .bss merges the zero-size key onto g_rsmc_fw_ctx.0).
Provider stub: //re/stubs_portA:pinctrl_msm_stub (msm_gpio_mpm_wake_set, header linux/pinctrl/qcom-pinctrl.h).
