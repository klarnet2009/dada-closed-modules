/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_log.h - reconstructed from the call sites of the stock Xiaomi MCA modules (dada).
 * Every message is "[<tag>]%s:%d <fmt>" with __func__ and __LINE__ prepended.  MCA_LOG_TAG is defined by each .c file.
 */
#ifndef __MCA_LOG_H__
#define __MCA_LOG_H__

void __mca_log_err(const char *format, ...);
void __mca_log_info(const char *format, ...);
void __mca_log_debug(const char *format, ...);

#define mca_log_err(fmt, ...) \
	__mca_log_err("[" MCA_LOG_TAG "]%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define mca_log_info(fmt, ...) \
	__mca_log_info("[" MCA_LOG_TAG "]%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)
#define mca_log_debug(fmt, ...) \
	__mca_log_debug("[" MCA_LOG_TAG "]%s:%d " fmt, __func__, __LINE__, ##__VA_ARGS__)

enum mca_charge_log_id_ele {
	MCA_CHARGE_LOG_ID_BUSINESS_CHG = 0,
	MCA_CHARGE_LOG_ID_THERMAL,
	MCA_CHARGE_LOG_ID_BATTERY_INFO,
	MCA_CHARGE_LOG_ID_FG_MASTER_IC,
	MCA_CHARGE_LOG_ID_FG_SLAVE_IC,
	MCA_CHARGE_LOG_ID_CP_MASTER_IC,
	MCA_CHARGE_LOG_ID_CP_SLAVE_IC,
	MCA_CHARGE_LOG_ID_USCP,
	MCA_CHARGE_LOG_ID_MAX,
};

struct mca_log_charge_log_ops {
	int (*dump_log_head)(void *data, char *buf, int size);
	int (*dump_log_context)(void *data, char *buf, int size);
};

void mca_log_charge_log_register(enum mca_charge_log_id_ele type,
	struct mca_log_charge_log_ops *ops, void *data);

#endif /* __MCA_LOG_H__ */
