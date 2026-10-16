// SPDX-License-Identifier: GPL-2.0
/* Stub provider of cs_dsp_load_coeff (not in the GKI cs_dsp). */
#include <linux/module.h>
#include <linux/firmware.h>
#include <linux/firmware/cirrus/cs_dsp.h>

int cs_dsp_load_coeff(struct cs_dsp *dsp, const struct firmware *firmware, const char *file) { return 0; }
EXPORT_SYMBOL_NS_GPL(cs_dsp_load_coeff, FW_CS_DSP);
MODULE_LICENSE("GPL");
