# stm_nfc_i2c - EQUIVALENT vs OS4 stock (26 functions byte-identical, relocations, objects, 58 imports + CRCs, modinfo)

Reference: OS4 `<work-dir>` (2 quick-loop iterations + 1 Bazel build).
Template: OnePlus13 `op13_modules: vendor/st/opensource/driver/nfc/st21nfc.{c,h}` + `driver/include/uapi/linux/nfc/st_uapi.h` (ST21NFC 2.2.0.19, same version string as the stock).
Securemsm headers `IClientEnv.h`, `smcinvoke_object.h`, `smcinvoke.h` are copies from `op13_modules: vendor/qcom/opensource/securemsm-kernel/include/linux/` (in `re/stubs_a17C/include`).
Changes relative to the template (from the stock disassembly):
- `NFC_SECURE_PERIPHERAL_ENABLED=1` as local define (stock has `init_flag`, `nfc_hw_secure_check`, `nfc_dynamic_protection_ioctl`, smcinvoke imports).
- `st21nfc_dev_read` / `st21nfc_dev_write`: the `i2c_master_recv/send returned %d` messages are `pr_info` (KERN_INFO "6") in the stock, template has `pr_err`.
- lock keys (`mutex_init`, `init_waitqueue_head`) redefined to live in one `.bss` section (the stock ThinLTO link merges the four probe keys to one address).
- `poll_wait`: kernel 6.6.144 added a trailing `smp_mb()` (`dmb ish`) to `poll_wait()`; the stock was built on 6.6.57 without it. The module uses an open-coded 6.6.57 `poll_wait` (`st21_poll_wait`) to be identical to the stock.
  This is the only place where our kernel header differs from the one the stock saw; behaviourally it equals the stock binary (without the barrier).
- Providers `get_client_env_object` (smcinvoke_dlkm) and `msm_gpio_mpm_wake_set` (pinctrl-msm) are stubs in `re/stubs_a17C` (`stub_smcinvoke`, `stub_pinctrl_msm`); import CRCs match.
- Build: ThinLTO `stm_nfc_i2c_defconfig`, `CONFIG_LTO=1`, function/data sections.
Tools: `work/re/src/a17C_tools/` (`a17c_push.sh`, `q.sh`).
