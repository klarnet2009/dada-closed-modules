// SPDX-License-Identifier: GPL-2.0
/* Stub providers of the exports qcom_adsp_pd_protocol.ko imports (mca_log, mca_event, mca_adsp_glink, mca_protocol_pd_class).
 * They exist only so that modpost resolves the imports and the export CRCs can be compared with the stock __versions. */
#include <linux/module.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_event.h>
#include <mca/qcom/mca_adsp_glink.h>
#include <mca/protocol/protocol_class_pd.h>

void __mca_log_err(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_err);
void __mca_log_info(const char *format, ...) { }
EXPORT_SYMBOL(__mca_log_info);
int mca_event_block_notify_register(unsigned int type, struct notifier_block *nb) { return 0; }
EXPORT_SYMBOL(mca_event_block_notify_register);
int mca_adsp_glink_write_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_write_prop);
int mca_adsp_glink_read_prop(unsigned int prop_id, void *data, int len) { return 0; }
EXPORT_SYMBOL(mca_adsp_glink_read_prop);
int protocol_class_pd_register_ops(unsigned int port_num, struct protocol_class_pd_ops *ops, void *data) { return 0; }
EXPORT_SYMBOL(protocol_class_pd_register_ops);
MODULE_LICENSE("GPL");
