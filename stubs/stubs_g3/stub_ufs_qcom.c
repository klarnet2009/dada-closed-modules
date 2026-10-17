// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the Xiaomi mi_ufs exports used by mi_memory (OS4 stock ufs_qcom.ko). The struct tags are forged
 * (g3_tools/forge1.py) so that genksyms yields the stock export CRCs without knowing the Xiaomi-forked prototypes. */
#include <linux/module.h>

struct fgyhucag;
void mi_ufshcd_read_desc_param(struct fgyhucag *p) {}
EXPORT_SYMBOL(mi_ufshcd_read_desc_param);
struct fgdJTcOr;
void mi_ufshcd_query_descriptor_retry(struct fgdJTcOr *p) {}
EXPORT_SYMBOL(mi_ufshcd_query_descriptor_retry);
struct fgZXUc90;
void get_ufs_data(struct fgZXUc90 *p) {}
EXPORT_SYMBOL(get_ufs_data);
MODULE_LICENSE("GPL");
