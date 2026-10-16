/* SPDX-License-Identifier: GPL-2.0 */
/* mca_adsp_glink.h - interface of the Xiaomi MCA <-> ADSP glink module (reconstructed from the stock BTF/disassembly) */
#ifndef __MCA_ADSP_GLINK_H__
#define __MCA_ADSP_GLINK_H__

#include <linux/types.h>

struct mca_adsp_call_back {
	void (*glink_down_cb)(void *priv);
	void (*sync_data_cb)(void *priv);
	void (*notify_cb)(int notify_id, void *data, int len, void *priv);
};

int mca_adsp_glink_write_prop(unsigned int prop_id, void *data, int len);
int mca_adsp_glink_read_prop(unsigned int prop_id, void *data, int len);
int mca_adsp_glink_qbg_write_prop(unsigned int prop_id, void *data, int len);
int mca_adsp_glink_qbg_read_prop(unsigned int prop_id, void *data, int len);
int mca_adsp_glink_resister_ops(struct mca_adsp_call_back *ops, void *priv);
int mca_adsp_glink_qbg_resister_ops(struct mca_adsp_call_back *ops, void *priv);

#endif /* __MCA_ADSP_GLINK_H__ */
