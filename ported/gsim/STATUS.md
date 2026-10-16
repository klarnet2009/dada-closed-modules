# gsim - EQUIVALENT vs OS4 stock (218 functions byte-identical, relocations, objects, 82 exports + CRCs, 82 imports + CRCs, modinfo)

Reference: OS4 `<work-dir>` (Qualcomm GSI driver of the data IPA, "Generic Software Interface"). Agent a17C, 4 quick-loop iterations + 1 Bazel build.
Template: OnePlus13 `op13_modules: vendor/qcom/opensource/dataipa/drivers/platform/msm/{gsi/gsi.c,gsi.h,gsi_dbg.c,gsi_trace.h,gsi_emulation*.h,gsihal/*}` + `include/linux/msm_gsi.h`.
The template matched the stock almost verbatim (0 source changes in gsi.c / gsi_dbg.c / gsihal*.c). Differences are build-environment only:
- `gsihal/` flattened into the package (`gsihal_i.h` include of `../gsi.h` -> `gsi.h`); `gsi_emulation.c` not built (no `CONFIG_IPA_EMULATION`); `gsi_dbg.c` built (`CONFIG_DEBUG_FS`).
- Defines: `CONFIG_GSI=1`, `CONFIG_DEBUG_FS=1`, `CONFIG_IPC_LOGGING=1`, `CONFIG_QCOM_VA_MINIDUMP=1` (stock imports `ipc_log_*` and `qcom_va_md_*`), `GSI_TRACE_INCLUDE_PATH=.`.
- `gsim_force.h` (forced with `-include`, uses `stubs_a17C/include/a17c_compat.h`):
  * mmio: 6.6.144's generic `readl_relaxed()/writel_relaxed()` gate `log_read_mmio()/log_write_mmio()` with `rwmmio_tracepoint_enabled()` (static key), the 6.6.57 headers of the stock called them unconditionally (the two out-of-line copies `readl_relaxed`/`writel_relaxed` differed). The helper re-defines both with the 6.6.57 shape (`noinline`, unconditional calls).
  * lock keys: `mutex_init`/`init_waitqueue_head`/`init_completion` keys are put into one explicit `.bss` section (stock ThinLTO link merges them: relocation target `gsi_register_device.__key`).
- Providers = stubs in `re/stubs_a17C`: `stub_ipc_logging`, `stub_va_minidump` (`qcom_va_md_register`, `qcom_va_md_add_region`); CRCs of the imports match the stock.
- Build: ThinLTO `gsim_defconfig`, `CONFIG_LTO=1`, function/data sections.
