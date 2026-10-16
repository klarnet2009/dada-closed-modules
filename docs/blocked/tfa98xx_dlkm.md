# tfa98xx_dlkm - BLOCKED (not built): no usable template in the available trees

Reference: OS4 `<work-dir>` (NXP/Goodix TFA98xx smart PA, 389 functions, imports `miev` + `machine_dlkm`; looked at it, did not iterate).

Template looked at: OnePlus13 `op13_modules: vendor/oplus/kernel/audio/codecs/tfa98xx/` (copied unmodified into this directory, the Makefile/feedback/`src/` variants are in `work/re/src/tpl_a17C/tfa98xx_oplus/`). Problems:
- It is the **MediaTek** flavour: `tfa98xx.c` includes `mtk-sp-spk-amp.h`, `mtk-dsp-common.h`, calls `mtk_spk_{send,recv}_ipi_buf_*`, defines `CONFIG_MTK_PLATFORM`; the dada stock has none of that (Qualcomm, plain regmap/i2c path, `check_smartpa_type`/`set_smartpa_type`/`set_smartpa_codec` from machine_dlkm, `mievent_report` via miev).
- 88 of the 389 stock function names are not in this template: the per-chip tables of newer devices (`tfa9865_*`, `tfa9875_*`, `tfa986x_*`, `tfa9878_*`, `tfa9912_update_lpm`, ...). The sibling tree `tfa98xx-v6/` (TFA v6 container, `*_v6.c`) has even fewer matches (177 missing) because of `_v6` renames.
- Estimated work: port the MTK glue away, add the newer chip support from the stock disassembly (~90 functions) and the Xiaomi additions; far more than the ~25-iteration budget for one module.
Nothing was compiled; the unmodified template is only kept so the next person does not have to refetch it.
