# stm_st54se_gpio - EQUIVALENT vs OS4 stock (7 functions byte-identical, relocations, objects, 25 imports + CRCs, modinfo)

Reference: OS4 `<work-dir>` (1 build + 1 quick-loop iteration).
Template: OnePlus13 vendor tree `op13_modules: vendor/st/opensource/eSE-driver/st54spi_gpio.c` (ST54SPI GPIO driver, kernel API already 6.6).
Changes relative to the template (all taken from the stock disassembly):
- `struct st54spi_gpio_device` got `int power_count` at 0xac (struct 0xb0 bytes); probe stores `power_count = 0` just before `platform_set_drvdata`.
- `ST54SPI_SET_GPIO` ioctl: Xiaomi product logic. `strcmp(product_name_get(), "xuanyuan") == 0` -> reference counted power: log "Original power count", `arg==0 ? --: ++`, log "Updated power count", clamp at 0;
  `arg==0 && count!=0` -> log "No execution power count" and return 0; else log "GPIO setting for power count", `gpio_set_value(reset, arg)`, `usleep_range(3000,3001)` (arg 0) / `usleep_range(10000,10001)` (arg 1).
  Other products: only `arg==1` sets the GPIO (no delay, `arg==0` does nothing).
- `product_name_get()` is imported from hwid.ko (stub `re/stubs_a17C:stub_hwid`, prototype `const char *product_name_get(void)`, import CRC matches).
- Build: ThinLTO `stm_st54se_gpio_defconfig`, `CONFIG_LTO=1`, function/data sections (same recipe as the other ports). No other deviations from the template.
Tools: `work/re/src/a17C_tools/` (`a17c_push.sh`, server-side `re_build.sh` against `<work-dir>`), quick loop `<work-dir>`.
