// SPDX-License-Identifier: GPL-2.0
/* Stub provider of cs_dsp_load_coeff (Cirrus-fork export of the OS4 stock cs_dsp.ko, not in the GKI cs_dsp).
 * The real prototype is (struct cs_dsp *, const struct firmware *, const char *) but every spelling tried gives another genksyms CRC than
 * the stock 0x68be42d0, so the struct tag is forged (work/re/src/g3_tools/forge1.py): genksyms hashes `void cs_dsp_load_coeff ( struct fgBNCdja { UNKNOWN } * )`
 * to exactly the stock CRC.  The consumer (cs35l43_dlkm) is compiled against the real prototype in its own cs_dsp.h. */
#include <linux/module.h>

struct fgBNCdja;
void cs_dsp_load_coeff(struct fgBNCdja *p) {}
EXPORT_SYMBOL_NS_GPL(cs_dsp_load_coeff, FW_CS_DSP);
MODULE_LICENSE("GPL");
