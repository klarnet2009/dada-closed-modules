// SPDX-License-Identifier: GPL-2.0
/* Stub provider of the mca_adsp_glink.ko exports (the reconstructed module itself could be used as well). */
#include <linux/module.h>
#include <mca/qcom/mca_adsp_glink.h>

int mca_adsp_glink_write_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_write_prop);
int mca_adsp_glink_read_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_read_prop);
int mca_adsp_glink_qbg_write_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_qbg_write_prop);
int mca_adsp_glink_qbg_read_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_qbg_read_prop);
int mca_adsp_glink_resister_ops(struct mca_adsp_call_back *ops, void *priv) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_resister_ops);
int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_call_back *ops, void *priv) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_qbg_resister_ops);
MODULE_LICENSE("GPL");
